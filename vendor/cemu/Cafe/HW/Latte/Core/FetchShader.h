#pragma once
#include "Cafe/HW/Latte/Core/LatteConst.h"

struct LatteParsedFetchShaderAttribute
{
	uint8								attributeBufferIndex;
	uint8								semanticId;
	Latte::E_HWFMT						format;
	LatteConst::VertexFetchType2		fetchType;
	uint8								nfa;
	uint8								isSigned;
	LatteConst::VertexFetchEndianMode	endianSwap;
	uint8								ds[4];
	sint32								aluDivisor;
	uint32								offset;
};

struct LatteParsedFetchShaderBufferGroup
{
	uint8 attributeBufferIndex{};
	sint8 attribCount{};
	bool hasVtxIndexAccess : 1;
	bool hasInstanceIndexAccess : 1;
	uint32 minOffset{};
	uint32 totalAttribRangeSize{};
	LatteParsedFetchShaderAttribute* attrib{};

	uint32 getCurrentBufferStride(uint32* contextRegister) const;
};

struct LatteFetchShader
{
	using CacheHash = uint64;

	~LatteFetchShader();

	std::vector<LatteParsedFetchShaderBufferGroup> bufferGroups;
	std::vector<LatteParsedFetchShaderBufferGroup> bufferGroupsInvalid;

	uint64 key{};
	uint32 attributeBufferMask{};

	uint64 vkPipelineHashFragment{};

	bool mtlFetchVertexManually{};

	CacheHash m_cacheHash{};
	bool m_isRegistered{};

	void CalculateFetchShaderVkHash();

#ifdef ENABLE_METAL
	void CheckIfVerticesNeedManualFetchMtl(uint32* contextRegister);
#endif

	uint64 getVkPipelineHashFragment() const { return vkPipelineHashFragment; };

	static bool isValidBufferIndex(const uint32 index) { return index < 0x10; };

	std::vector<uint64> m_shaderStateCacheKeys;

	LatteFetchShader* RegisterInCache(CacheHash fsHash);
	void UnregisterInCache();
	static CacheHash CalculateCacheHash(void* programCode, uint32 programSize);
	static LatteFetchShader* FindInCacheByHash(CacheHash fsHash);
	static LatteFetchShader* FindByGPUState();

	static std::unordered_map<CacheHash, LatteFetchShader*> s_fetchShaderByHash;
};

LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash fsHash, uint32* contextRegister, uint32* fsProgramCode, uint32 fsProgramSize);
