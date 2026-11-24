interface SceneFactory {
    Boot: number;
    Flow: number;
    FileSelect: number;
    SaveSelect: number;
    Title: number;
    Menu: number;
    Map: number;
    CourseTask: number;
    CourseSelect: number;
}

interface Scene {
    Factory: SceneFactory;
    title(this: void): Result<boolean>;
    file_select(this: void): Result<boolean>;
    save_select(this: void): Result<boolean>;
    course_task(this: void): Result<boolean>;
    file_index(this: void): Result<number>;
    flow_phase(this: void): Result<boolean>;
    map_phase(this: void): Result<boolean>;
    phase(this: void): Result<number>;
    course_select(this: void): Result<boolean>;
    course(this: void): Result<boolean>;
    open_title(this: void): Result<boolean>;
    apply_save(
        this: void,
        file?: number,
        world?: number,
        course?: number,
    ): Result<boolean>;
    open_first_save(this: void, file?: number): Result<boolean>;
    open_file_select(this: void): Result<boolean>;
    open_course_select(this: void): Result<boolean>;
    leave_course(this: void): Result<boolean>;
    confirm_file(this: void): Result<boolean>;
    load_story(this: void, world_no: number, course_no: number): Result<boolean>;
    load_pack(this: void, name: string): Result<boolean>;
}
