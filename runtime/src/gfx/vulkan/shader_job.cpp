#include "shader_job.h"
#include "shaders.h"
#include "settings.h"
#include "platform/host.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gfxvk::vk {
namespace {
struct Worker {
    std::mutex mutex;
    std::condition_variable wake;
    struct Item {
        std::string source;
        bool vertex = false;
        std::shared_ptr<SpirvCompile> done;
    };
    std::deque<Item> queue;
    std::thread thread;
    std::thread::id self;
    bool stop = false;
    bool joining = false;
    void finish() {
        std::thread joiningThread;
        {
            std::lock_guard lock(mutex);
            if (thread.joinable() && thread.get_id() == std::this_thread::get_id())
                throw std::logic_error("shader worker cannot join itself");
            stop = true;
            joining = true;
            joiningThread = std::move(thread);
            self = {};
        }
        wake.notify_all();
        if (joiningThread.joinable()) joiningThread.join();
        std::lock_guard lock(mutex);
        joining = false;
        stop = false;
    }
    ~Worker() { finish(); }
};
Worker& worker() {
    static Worker value;
    return value;
}
void run(Worker& w) {
    try { host::set_thread_name("Vulkan shader"); } catch (...) {}
    for (;;) {
        Worker::Item item;
        {
            std::unique_lock lock(w.mutex);
            w.wake.wait(lock, [&] { return w.stop || !w.queue.empty(); });
            if (w.queue.empty()) return;
            item = std::move(w.queue.front());
            w.queue.pop_front();
        }
        if (!item.done) continue;
        std::vector<uint32_t> compiled;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        try { compiled = compile_glsl(item.source, item.vertex, &error); }
        catch (...) { error = "SPIR-V compile failed"; }
        SpirvWords words;
        if (!compiled.empty())
            words = std::make_shared<const std::vector<uint32_t>>(std::move(compiled));
        const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        std::lock_guard doneLock(item.done->mutex);
        item.done->words = std::move(words);
        item.done->error = std::move(error);
        item.done->ns = ns;
        item.done->done = true;
    }
}
std::mutex pipelineMutex;
}
bool shader_jobs_async() { return async_shaders_enabled(); }
void enqueue_spirv_compile(std::string source, bool vertex, std::shared_ptr<SpirvCompile> done) {
    if (!done) return;
    auto& w = worker();
    {
        std::lock_guard lock(w.mutex);
        if (w.joining) {
            std::lock_guard doneLock(done->mutex);
            done->error = "shader compile shutdown";
            done->done = true;
            return;
        }
        w.queue.push_back(Worker::Item{std::move(source), vertex, std::move(done)});
        if (!w.thread.joinable()) {
            w.stop = false;
            w.thread = std::thread(run, std::ref(w));
            w.self = w.thread.get_id();
        }
    }
    w.wake.notify_one();
}
void finish_shader_jobs() { worker().finish(); }
VkResult create_graphics_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                   const VkGraphicsPipelineCreateInfo* info,
                                   const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    std::lock_guard lock(pipelineMutex);
    return vkCreateGraphicsPipelines(device, cache, count, info, allocator, pipelines);
}
}
