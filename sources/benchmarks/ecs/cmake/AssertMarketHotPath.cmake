if(NOT DEFINED HOT_PATH_SOURCE)
    message(FATAL_ERROR "HOT_PATH_SOURCE is required")
endif()

file(READ "${HOT_PATH_SOURCE}" source_text)

set(forbidden_patterns
    "ForEachMutable<BenchPosition>"
    "ForEachMutable<BenchWorldTransform>"
    "TryGet<BenchVelocity>"
    "TryGet<BenchStructuralMarker>"
    "TryGet<BenchSystemChainState>"
    "TryGetMutable<BenchSystemChainState>"
    "TryGetMutable<BenchLocalTransform>"
    "TryGetMutable<BenchHierarchyNode>"
    "TryGetMutable<BenchWorldTransform>"
)

foreach(pattern IN LISTS forbidden_patterns)
    string(FIND "${source_text}" "${pattern}" pattern_index)
    if(NOT pattern_index EQUAL -1)
        message(FATAL_ERROR "Market ECS hot path must use multi-component batches, found forbidden pattern: ${pattern}")
    endif()
endforeach()

foreach(benchmark_telemetry_contract IN ITEMS
        "double workerUtilizationPercent = std::numeric_limits<double>::quiet_NaN();"
        "ValidateBenchmarkExecutionTelemetry"
        "std::isfinite(result.workerUtilizationPercent)"
        "result.telemetryQueryBytesTouched > 0U"
        "std::isfinite(result.telemetryQueryBytesPerSecond)"
        "result.telemetryQueryBytesPerSecond <= 0.0"
        "valid query telemetry bandwidth"
        "CopyBenchmarkRuntimeTelemetry"
        "result.telemetryQueryElapsedNanoseconds = source.telemetryQueryElapsedNanoseconds;"
        "result.telemetryQueryBytesPerSecond = source.telemetryQueryBytesPerSecond;"
        "result.telemetryQueryBytesPerSecond = telemetry.queryEstimatedBytesPerSecond;"
        "result.telemetryCompatMutableIterations = telemetry.compatMutableIterations;"
        "result.telemetryCompatMutableEntitiesVisited = telemetry.compatMutableEntitiesVisited;"
        "\"telemetry_compat_mutable_iterations\""
        "\"telemetry_compat_mutable_entities_visited\""
        "\"telemetry_query_elapsed_nanoseconds\""
        "result.telemetryTotalStructuralChanges = source.telemetryTotalStructuralChanges;"
        "const std::size_t resultBackendLaneCount = KernelBackendLaneCountForName(result.simdBackend);"
        "result.simdBackendLaneCount = resultBackendLaneCount;"
        "CopyBenchmarkRuntimeTelemetry(result, measuredStats);"
        ".executionPolicy = \"prefab_bulk_spawn\""
        "result.executionPolicy = \"prefab_registry_record\";"
        ".executionPolicy = \"prefab_override\"")
    string(FIND "${source_text}" "${benchmark_telemetry_contract}" benchmark_telemetry_contract_index)
    if(benchmark_telemetry_contract_index EQUAL -1)
        message(FATAL_ERROR "Market benchmark execution telemetry must be explicit and validated, missing: ${benchmark_telemetry_contract}")
    endif()
endforeach()

foreach(packed_kernel_contract IN ITEMS
        "KB_ECS_BENCHMARK_HAS_SSE2"
        "UpdatePositionVelocityPackedSse2"
        "_mm_loadu_ps(positionValues)"
        "_mm_mul_ps(velocity, delta)"
        "_mm_storeu_ps(positionValues, updated)"
        "packedSse2Update();")
    string(FIND "${source_text}" "${packed_kernel_contract}" packed_kernel_contract_index)
    if(packed_kernel_contract_index EQUAL -1)
        message(FATAL_ERROR "Position+Velocity SIMD benchmark must keep the packed x86 hot kernel, missing: ${packed_kernel_contract}")
    endif()
endforeach()

foreach(prefab_registry_record_contract IN ITEMS
        "std::string_view{ \"prefab_registry_record\" }"
        "RunPrefabRegistryRecordBenchmark"
        "RunPrefabRegistryRecordFrame"
        "kb::scene::ScenePrefabInstanceRegistry registry;"
        "registry.RegisterManyInstances("
        "registry.FindRootInstance(root)"
        "registry.FindContainingInstance(leaf, nodeIndex, nodeId)"
        "BenchmarkCategoryEnabled(options, \"prefab_registry_record\")")
    string(FIND "${source_text}" "${prefab_registry_record_contract}" prefab_registry_record_contract_index)
    if(prefab_registry_record_contract_index EQUAL -1)
        message(FATAL_ERROR "Prefab registry record benchmark must isolate registry-only hot path, missing: ${prefab_registry_record_contract}")
    endif()
endforeach()

string(FIND "${source_text}" "std::make_unique<kb::scene::Scene>()" default_benchmark_scene_index)
if(NOT default_benchmark_scene_index EQUAL -1)
    message(FATAL_ERROR "Scene benchmarks must construct scenes with MakeBenchmarkWorldConfig(options), not the default scene world")
endif()

foreach(scene_benchmark_config_contract IN ITEMS
        "CreatePrefabSpawnBenchmarkData("
        "CreatePrefabRegistryRecordBenchmarkData("
        "CreatePrefabOverrideBenchmarkData("
        "CreateSceneRenderSyncBenchmarkData"
        "CreateSceneTransformFastPathBenchmarkData"
        "std::make_unique<kb::scene::Scene>(MakeBenchmarkWorldConfig(options))")
    string(FIND "${source_text}" "${scene_benchmark_config_contract}" scene_benchmark_config_index)
    if(scene_benchmark_config_index EQUAL -1)
        message(FATAL_ERROR "Scene benchmark world config contract is missing: ${scene_benchmark_config_contract}")
    endif()
endforeach()

string(FIND "${source_text}" "bool prefabSyncWorldHierarchy = false;" prefab_default_scene_hierarchy_index)
if(prefab_default_scene_hierarchy_index EQUAL -1)
    message(FATAL_ERROR "Prefab spawn benchmark must default to fast scene hierarchy; backend hierarchy sync must be explicit opt-in")
endif()

if(DEFINED SCENE_PREFAB_INSTANTIATION_SETTINGS_HEADER)
    file(READ "${SCENE_PREFAB_INSTANTIATION_SETTINGS_HEADER}" scene_prefab_instantiation_settings_text)
    string(FIND "${scene_prefab_instantiation_settings_text}" "bool syncWorldHierarchy = false;" prefab_settings_default_scene_hierarchy_index)
    if(prefab_settings_default_scene_hierarchy_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab instantiation settings must default to fast scene hierarchy; backend hierarchy sync must be explicit opt-in")
    endif()
endif()

string(FIND "${source_text}" "double workerUtilizationPercent = 100.0;" default_worker_utilization_index)
if(NOT default_worker_utilization_index EQUAL -1)
    message(FATAL_ERROR "BenchmarkResult must not default worker utilization to 100%; each benchmark must report it explicitly")
endif()

string(FIND "${source_text}" "RunStructuralChangesFrame" structural_function_begin)
if(structural_function_begin EQUAL -1)
    message(FATAL_ERROR "Market ECS hot path guard could not find RunStructuralChangesFrame")
endif()
string(SUBSTRING "${source_text}" ${structural_function_begin} -1 structural_function_tail)
string(FIND "${structural_function_tail}" "constexpr kb::ecs::ComponentId kNativeBulkPositionId" structural_function_end)
if(structural_function_end EQUAL -1)
    message(FATAL_ERROR "Market ECS hot path guard could not bound RunStructuralChangesFrame")
endif()
string(SUBSTRING "${structural_function_tail}" 0 ${structural_function_end} structural_function_body)
string(FIND "${structural_function_body}" "options.debugValidationEnabled" structural_validation_gate_index)
foreach(structural_lookup IN ITEMS
        "data.world.IsAlive"
        "data.world.Has<BenchPosition>"
        "data.world.Has<BenchVelocity>")
    string(FIND "${structural_function_body}" "${structural_lookup}" structural_lookup_index)
    if(NOT structural_lookup_index EQUAL -1)
        if(structural_validation_gate_index EQUAL -1 OR structural_lookup_index LESS structural_validation_gate_index)
            message(FATAL_ERROR "Structural benchmark release hot path must not run per-entity ${structural_lookup}; keep it behind debug validation")
        endif()
    endif()
endforeach()

foreach(function_name IN ITEMS "MarkHierarchyRootsDirty" "PropagateHierarchyTransforms")
    string(FIND "${source_text}" "${function_name}" function_begin)
    if(function_begin EQUAL -1)
        message(FATAL_ERROR "Market ECS hot path guard could not find ${function_name}")
    endif()
    string(SUBSTRING "${source_text}" ${function_begin} -1 function_tail)
    string(FIND "${function_tail}" "RegisterStructuralComponents" function_end)
    if(function_end EQUAL -1)
        message(FATAL_ERROR "Market ECS hot path guard could not bound ${function_name}")
    endif()
    string(SUBSTRING "${function_tail}" 0 ${function_end} function_body)
    string(FIND "${function_body}" "TryGet" lookup_index)
    if(NOT lookup_index EQUAL -1)
        message(FATAL_ERROR "Market ECS hierarchy hot path must use cached component pointers, found TryGet in ${function_name}")
    endif()
endforeach()

if(DEFINED SCENE_RENDER_SYNC_SOURCE)
    file(READ "${SCENE_RENDER_SYNC_SOURCE}" scene_source_text)
    string(FIND "${scene_source_text}" "SceneComponentIterationAccess::TryGet" scene_lookup_index)
    if(NOT scene_lookup_index EQUAL -1)
        message(FATAL_ERROR "Scene render sync hot path must iterate component fields, found per-entity TryGet")
    endif()
    string(FIND "${scene_source_text}" "ecs_query_iter" scene_query_index)
    if(scene_query_index EQUAL -1)
        message(FATAL_ERROR "Scene render sync hot path guard did not find query iteration")
    endif()
    string(FIND "${scene_source_text}" "cachedQuery" scene_cached_query_index)
    if(scene_cached_query_index EQUAL -1)
        message(FATAL_ERROR "Scene render sync hot path guard did not find cached query reuse")
    endif()
    string(FIND "${scene_source_text}" "ecs_query_fini" scene_query_fini_index)
    if(NOT scene_query_fini_index EQUAL -1)
        message(FATAL_ERROR "Scene render sync hot path must not finalize its query per iteration")
    endif()
    string(FIND "${scene_source_text}" "SceneComponentIterationAccess::Field<TransformComponent>" scene_transform_field_index)
    if(scene_transform_field_index EQUAL -1)
        message(FATAL_ERROR "Scene render sync hot path guard did not find transform column access")
    endif()
endif()

if(DEFINED SCENE_INPUT_ACTIVATION_SOURCE)
    file(READ "${SCENE_INPUT_ACTIVATION_SOURCE}" scene_input_activation_source_text)
    foreach(scene_input_forbidden_pattern IN ITEMS
            "ecs_each_id"
            "SceneComponentIterationAccess::Field<InputComponent>")
        string(FIND "${scene_input_activation_source_text}" "${scene_input_forbidden_pattern}" scene_input_forbidden_index)
        if(NOT scene_input_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene input activation hot path must use typed input batches, found forbidden pattern: ${scene_input_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_input_activation_source_text}" "CreateQuery<InputComponent>" scene_input_query_index)
    if(scene_input_query_index EQUAL -1)
        message(FATAL_ERROR "Scene input activation guard did not find typed input query")
    endif()
    string(FIND "${scene_input_activation_source_text}" "ForEachBatch" scene_input_batch_index)
    if(scene_input_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene input activation guard did not find batch iteration")
    endif()
endif()

if(DEFINED SCENE_BEHAVIOUR_ITERATION_SOURCE)
    file(READ "${SCENE_BEHAVIOUR_ITERATION_SOURCE}" scene_behaviour_iteration_source_text)
    foreach(scene_behaviour_forbidden_pattern IN ITEMS
            "ecs_each_id"
            "SceneComponentIterationAccess::Field<BehaviourComponent>")
        string(FIND "${scene_behaviour_iteration_source_text}" "${scene_behaviour_forbidden_pattern}" scene_behaviour_forbidden_index)
        if(NOT scene_behaviour_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene behaviour iteration hot path must use typed behaviour batches, found forbidden pattern: ${scene_behaviour_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_behaviour_iteration_source_text}" "CreateQuery<BehaviourComponent>" scene_behaviour_query_index)
    if(scene_behaviour_query_index EQUAL -1)
        message(FATAL_ERROR "Scene behaviour iteration guard did not find typed behaviour query")
    endif()
    string(FIND "${scene_behaviour_iteration_source_text}" "ForEachBatch" scene_behaviour_batch_index)
    if(scene_behaviour_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene behaviour iteration guard did not find batch iteration")
    endif()
endif()

if(DEFINED SCENE_TRANSFORM_COMPONENT_HEADER AND DEFINED SCENE_HIERARCHY_TEST_SOURCE)
    file(READ "${SCENE_TRANSFORM_COMPONENT_HEADER}" scene_transform_component_header_text)
    file(READ "${SCENE_HIERARCHY_TEST_SOURCE}" scene_hierarchy_test_source_text)
    foreach(scene_transform_split_payload_pattern IN ITEMS
            "struct LocalTransform"
            "struct WorldTransform"
            "struct TransformVersionMetadata"
            "struct TransformHierarchyRelation"
            "LocalPayload() const noexcept"
            "WorldPayload() const noexcept"
            "VersionMetadata() const noexcept"
            "static constexpr TransformComponent FromPayloads")
        string(FIND "${scene_transform_component_header_text}" "${scene_transform_split_payload_pattern}" scene_transform_split_payload_index)
        if(scene_transform_split_payload_index EQUAL -1)
            message(FATAL_ERROR "Scene transform runtime must expose split local/world/metadata payload contract, missing: ${scene_transform_split_payload_pattern}")
        endif()
    endforeach()
    foreach(scene_transform_split_test_pattern IN ITEMS
            "RunTransformSplitPayloadContractTest"
            "RunTransformSplitPayloadContractTest();"
            "TransformComponent::FromPayloads(local, world, metadata)"
            "transform.LocalPayload()"
            "transform.WorldPayload()"
            "transform.VersionMetadata()"
            "TransformHierarchyRelation relation")
        string(FIND "${scene_hierarchy_test_source_text}" "${scene_transform_split_test_pattern}" scene_transform_split_test_index)
        if(scene_transform_split_test_index EQUAL -1)
            message(FATAL_ERROR "Scene transform split payload contract must stay covered by scene hierarchy tests, missing: ${scene_transform_split_test_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_TRANSFORM_HIERARCHY_SOURCE)
    file(READ "${SCENE_TRANSFORM_HIERARCHY_SOURCE}" scene_transform_hierarchy_source_text)
    foreach(scene_transform_forbidden_pattern IN ITEMS
            "TryGet<TransformComponent>"
            "TryGetMutable<TransformComponent>"
            "SceneComponentStorageAccess::TryGet<TransformComponent>"
            "SceneComponentStorageAccess::TryGetMutable<TransformComponent>")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_forbidden_pattern}" scene_transform_forbidden_index)
        if(NOT scene_transform_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy hot path must use batch query caches, found forbidden pattern: ${scene_transform_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_hierarchy_source_text}" "CreateQuery<TransformComponent>" scene_transform_query_index)
    if(scene_transform_query_index EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard did not find typed transform query")
    endif()
    string(FIND "${scene_transform_hierarchy_source_text}" "BuildTransformValueCache" scene_transform_cache_begin)
    if(scene_transform_cache_begin EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard did not find transform cache builder")
    endif()
    string(SUBSTRING "${scene_transform_hierarchy_source_text}" ${scene_transform_cache_begin} -1 scene_transform_cache_tail)
    string(FIND "${scene_transform_cache_tail}" "TransformFlushStats FlushDirtyTransforms" scene_transform_cache_end)
    if(scene_transform_cache_end EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard could not bound transform cache builder")
    endif()
    string(SUBSTRING "${scene_transform_cache_tail}" 0 ${scene_transform_cache_end} scene_transform_cache_body)
    string(FIND "${scene_transform_cache_body}" "ForEachBatch" scene_transform_cache_read_batch_index)
    if(scene_transform_cache_read_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene transform cache builder must use read-only batch iteration")
    endif()
    string(FIND "${scene_transform_cache_body}" "ForEachMutableBatch" scene_transform_cache_mutable_batch_index)
    if(NOT scene_transform_cache_mutable_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene transform cache builder must not mark transforms mutable while only reading")
    endif()
    foreach(scene_transform_cache_scratch_pattern IN ITEMS
            ".dense = state.transformValueDenseScratch"
            ".sparse = state.transformValueSparseScratch"
            "++state.transformValueCacheBuildVersion"
            ".buildVersion = state.transformValueCacheBuildVersion"
            ".denseLimit = state.denseHierarchyParents.size()"
            "cache.sparse.clear();"
            "cache.dense.size() < cache.denseLimit"
            "cache.dense.resize(cache.denseLimit)"
            "cache.sparse.reserve(state.hierarchyOrder.size())")
        string(FIND "${scene_transform_cache_body}" "${scene_transform_cache_scratch_pattern}" scene_transform_cache_scratch_index)
        if(scene_transform_cache_scratch_index EQUAL -1)
            message(FATAL_ERROR "Scene transform cache builder must reuse SceneState cache scratch, missing: ${scene_transform_cache_scratch_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_cache_body}" "cache.dense.clear();" scene_transform_cache_dense_clear_index)
    if(NOT scene_transform_cache_dense_clear_index EQUAL -1)
        message(FATAL_ERROR "Scene transform dense cache builder must not clear/default-fill the full dense vector per update")
    endif()
    foreach(scene_transform_cache_local_pattern IN ITEMS
            "std::vector<DenseEntry> dense;"
            "std::unordered_map<SceneEntity::IdType, DenseEntry> sparse;")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_cache_local_pattern}" scene_transform_cache_local_index)
        if(NOT scene_transform_cache_local_index EQUAL -1)
            message(FATAL_ERROR "Scene transform cache must not own local containers per update: ${scene_transform_cache_local_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_hierarchy_source_text}" "ForEachMutableBatch" scene_transform_batch_index)
    if(scene_transform_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard did not find mutable batch iteration")
    endif()
    foreach(scene_transform_scratch_reuse_pattern IN ITEMS
            "state.transformHierarchyEntriesScratch"
            "state.transformHierarchyUpdatedEntitiesScratch"
            "state.transformHierarchyApplyChunkStatsScratch")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_scratch_reuse_pattern}" scene_transform_scratch_reuse_index)
        if(scene_transform_scratch_reuse_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy must reuse SceneState scratch vectors, missing: ${scene_transform_scratch_reuse_pattern}")
        endif()
    endforeach()
    foreach(scene_transform_local_vector_pattern IN ITEMS
            "std::vector<SceneTransformBatchEntry> entries;"
            "std::vector<SceneEntity> updatedEntities;"
            "std::vector<TransformApplyChunkStats> chunkStats"
            "std::vector<SceneTransformApplyChunkStats> chunkStats")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_local_vector_pattern}" scene_transform_local_vector_index)
        if(NOT scene_transform_local_vector_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy must not allocate local propagation vectors each update: ${scene_transform_local_vector_pattern}")
        endif()
    endforeach()
    foreach(scene_transform_sparse_flush_pattern IN ITEMS
            "kSparseTransformFlushLookupFactor"
            "ShouldEvaluateTransform"
            "CanUseRootHierarchyDirtyFrontier"
            "RunRootHierarchyDirtyFrontier"
            "AppendUpdatedChildrenToFrontier"
            "transformDirtyFrontierLevelScratch"
            "transformDirtyFrontierNextScratch"
            "transformDirtyFrontierEntities"
            "lastTransformHierarchyDirtyFrontierCount"
            "parentDirty || transform.worldDirty || transform.parentVersion != parentWorldVersion"
            "TransformFlushStats"
            "TransformFlushContext"
            "updatedEntities.size() * kSparseTransformFlushLookupFactor < transformValues.TrackedCount()"
            "kDirtyListTransformFlushFactor"
            "state.world.SetMany<TransformComponent>"
            "transformHierarchyFlushComponentsScratch"
            "stats.sparseFlushCount = 1U"
            "stats.dirtyListFlushCount = 1U"
            "stats.batchFlushCount = 1U"
            "++stats.flushedEntityCount"
            "state.lastTransformHierarchySparseFlushCount = flushStats.sparseFlushCount"
            "state.lastTransformHierarchyDirtyListFlushCount = flushStats.dirtyListFlushCount"
            "state.lastTransformHierarchyDirtyListFlushEntityCount = flushStats.dirtyListFlushEntityCount"
            "state.lastTransformHierarchyBatchFlushCount = flushStats.batchFlushCount"
            "state.lastTransformHierarchyFlushedEntityCount = flushStats.flushedEntityCount")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_sparse_flush_pattern}" scene_transform_sparse_flush_index)
        if(scene_transform_sparse_flush_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy must keep sparse dirty flush for low dirty counts, missing: ${scene_transform_sparse_flush_pattern}")
        endif()
    endforeach()
    foreach(scene_transform_dense_scratch_pattern IN ITEMS
            "StoreTransformScratch"
            "denseTransformWorldScratch"
            "denseTransformWorldScratchValid"
            "denseTransformDirtyScratch"
            "ParentTransformOf"
            "ParentDirtyOf"
            "ParentWorldVersionOf")
        string(FIND "${scene_transform_hierarchy_source_text}" "${scene_transform_dense_scratch_pattern}" scene_transform_dense_scratch_index)
        if(scene_transform_dense_scratch_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy must keep dense parent scratch fast path, missing: ${scene_transform_dense_scratch_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_hierarchy_source_text}" "parentDirty ? 0U : ParentWorldVersionOf" scene_transform_parent_version_skip_index)
    if(scene_transform_parent_version_skip_index EQUAL -1)
        message(FATAL_ERROR "Scene transform entry build must skip parent version lookup when parent dirty already forces update")
    endif()
    string(FIND "${scene_transform_hierarchy_source_text}" "const std::uint64_t parentWorldVersion = ParentWorldVersionOf" scene_transform_parent_version_unconditional_index)
    if(NOT scene_transform_parent_version_unconditional_index EQUAL -1)
        message(FATAL_ERROR "Scene transform entry build must not compute parent version unconditionally")
    endif()
    string(FIND "${scene_transform_hierarchy_source_text}" "bool CanUseParallelDenseApply" scene_transform_dense_apply_begin)
    if(scene_transform_dense_apply_begin EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard did not find dense apply selector")
    endif()
    string(SUBSTRING "${scene_transform_hierarchy_source_text}" ${scene_transform_dense_apply_begin} -1 scene_transform_dense_apply_tail)
    string(FIND "${scene_transform_dense_apply_tail}" "void ApplyTransformEntries" scene_transform_dense_apply_end)
    if(scene_transform_dense_apply_end EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard could not bound dense apply selector")
    endif()
    string(SUBSTRING "${scene_transform_dense_apply_tail}" 0 ${scene_transform_dense_apply_end} scene_transform_dense_apply_body)
    foreach(scene_transform_dense_apply_forbidden_pattern IN ITEMS
            "for (const SceneTransformBatchEntry& entry : entries)"
            "GeneratedEntityIndex(entry.entity)"
            "&cached.transform != entry.transform")
        string(FIND "${scene_transform_dense_apply_body}" "${scene_transform_dense_apply_forbidden_pattern}" scene_transform_dense_apply_forbidden_index)
        if(NOT scene_transform_dense_apply_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene transform dense apply selector must stay O(1), found: ${scene_transform_dense_apply_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_hierarchy_source_text}" "void StoreTransformScratch" scene_transform_store_begin)
    if(scene_transform_store_begin EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard did not find StoreTransformScratch")
    endif()
    string(SUBSTRING "${scene_transform_hierarchy_source_text}" ${scene_transform_store_begin} -1 scene_transform_store_tail)
    string(FIND "${scene_transform_store_tail}" "void EnsureWorkerPool" scene_transform_store_end)
    if(scene_transform_store_end EQUAL -1)
        message(FATAL_ERROR "Scene transform hierarchy guard could not bound StoreTransformScratch")
    endif()
    math(EXPR scene_transform_after_store_begin "${scene_transform_store_begin} + ${scene_transform_store_end}")
    string(SUBSTRING "${scene_transform_hierarchy_source_text}" 0 ${scene_transform_store_begin} scene_transform_before_store)
    string(SUBSTRING "${scene_transform_hierarchy_source_text}" ${scene_transform_after_store_begin} -1 scene_transform_after_store)
    foreach(scene_transform_direct_scratch_pattern IN ITEMS
            "state.transformWorldScratch["
            "state.transformDirtyScratch[")
        string(FIND "${scene_transform_before_store}" "${scene_transform_direct_scratch_pattern}" scene_transform_direct_before_store_index)
        if(NOT scene_transform_direct_before_store_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy must route scratch writes through dense StoreTransformScratch")
        endif()
        string(FIND "${scene_transform_after_store}" "${scene_transform_direct_scratch_pattern}" scene_transform_direct_after_store_index)
        if(NOT scene_transform_direct_after_store_index EQUAL -1)
            message(FATAL_ERROR "Scene transform hierarchy update loop must route scratch writes through dense StoreTransformScratch")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_PREFAB_BULK_INSTANTIATION_SOURCE)
    file(READ "${SCENE_PREFAB_BULK_INSTANTIATION_SOURCE}" scene_prefab_bulk_source_text)
    foreach(scene_prefab_bulk_required_pattern IN ITEMS
            "BuildNodeNames"
            "SceneEntityNaming::SetRepeatedNames"
            "SceneEntityNaming::SetRepeatedNames(state, resolvedEntities"
            "entityCreateNanoseconds"
            "hierarchyRecordNanoseconds"
            "nameAssignmentNanoseconds"
            "constexpr bool nativeOnlyBatch = true;"
            "AddPrefabHierarchyDense(state, nodes, resolvedEntities, settings, instanceCount);"
            "SceneHierarchyCache::AssignDenseOrderRange(state, entities);"
            "state.denseHierarchyOrder.resize(required);"
            "std::vector<std::size_t>& childrenPerNode = state.prefabHierarchyChildrenPerNodeScratch;"
            "childrenPerNode.assign(nodes.size(), 0U);")
        string(FIND "${scene_prefab_bulk_source_text}" "${scene_prefab_bulk_required_pattern}" scene_prefab_bulk_required_index)
        if(scene_prefab_bulk_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab bulk spawn must keep repeated-name dense hierarchy fast path, missing: ${scene_prefab_bulk_required_pattern}")
        endif()
    endforeach()
    foreach(scene_prefab_bulk_forbidden_pattern IN ITEMS
            "std::vector<SceneEntity> hierarchyParents;"
            "std::vector<std::string> hierarchyNames;"
            "std::vector<std::size_t> childrenPerNode(nodes.size(), 0U);"
            "hierarchyNames.reserve(TotalNodeCount"
            "hierarchyNames.push_back"
            "SceneEntityNaming::SetRepeatedNames(state.world"
            "const bool nativeOnlyBatch = !settings.assignNames"
            "if (archetypes.size() == 1U && HasNaturalPrefabNodeOrder(archetypes.front()))"
            "SceneHierarchyCache::AssignOrderRange"
            "SceneHierarchyCache::AddMany(state")
        string(FIND "${scene_prefab_bulk_source_text}" "${scene_prefab_bulk_forbidden_pattern}" scene_prefab_bulk_forbidden_index)
        if(NOT scene_prefab_bulk_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab bulk spawn must not rebuild per-entity hierarchy/name buffers, found: ${scene_prefab_bulk_forbidden_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_STATE_HEADER AND DEFINED SCENE_HIERARCHY_CACHE_HEADER AND DEFINED SCENE_PREFAB_BULK_INSTANTIATION_SOURCE)
    file(READ "${SCENE_STATE_HEADER}" scene_state_dense_hierarchy_text)
    file(READ "${SCENE_HIERARCHY_CACHE_HEADER}" scene_hierarchy_cache_text)
    file(READ "${SCENE_PREFAB_BULK_INSTANTIATION_SOURCE}" scene_prefab_bulk_dense_hierarchy_text)

    foreach(scene_dense_hierarchy_required_pattern IN ITEMS
            "std::vector<std::uint64_t> denseHierarchyOrder;"
            "std::vector<std::size_t> prefabHierarchyChildrenPerNodeScratch;"
            "static void AssignOrder(SceneState& state, SceneEntity entity)"
            "static void AssignDenseOrderRange(SceneState& state, std::span<const SceneEntity> entities)"
            "state.denseHierarchyOrder[index] = order;"
            "state.denseHierarchyOrder[index] = 0U;"
            "SceneHierarchyCache::AssignDenseOrderRange(state, entities);")
        string(FIND "${scene_state_dense_hierarchy_text}\n${scene_hierarchy_cache_text}\n${scene_prefab_bulk_dense_hierarchy_text}" "${scene_dense_hierarchy_required_pattern}" scene_dense_hierarchy_required_index)
        if(scene_dense_hierarchy_required_index EQUAL -1)
            message(FATAL_ERROR "Scene hierarchy cache must keep dense order storage for prefab/runtime hot path, missing: ${scene_dense_hierarchy_required_pattern}")
        endif()
    endforeach()

    foreach(scene_dense_hierarchy_forbidden_pattern IN ITEMS
            "state.hierarchyOrder[entity.Id()] = firstOrder + EntityIndex"
            "state.hierarchyOrder.reserve(state.hierarchyOrder.size() + entities.size());")
        string(FIND "${scene_prefab_bulk_dense_hierarchy_text}\n${scene_hierarchy_cache_text}" "${scene_dense_hierarchy_forbidden_pattern}" scene_dense_hierarchy_forbidden_index)
        if(NOT scene_dense_hierarchy_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene hierarchy prefab hot path must not write order through sparse map for dense entities, found: ${scene_dense_hierarchy_forbidden_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_ENTITY_NAMING_SOURCE AND DEFINED SCENE_ENTITY_NAME_SERVICE_SOURCE AND DEFINED SCENE_ENTITY_DESTRUCTION_SOURCE AND DEFINED SCENE_STATE_HEADER)
    file(READ "${SCENE_ENTITY_NAMING_SOURCE}" scene_entity_naming_source_text)
    file(READ "${SCENE_ENTITY_NAME_SERVICE_SOURCE}" scene_entity_name_service_source_text)
    file(READ "${SCENE_ENTITY_DESTRUCTION_SOURCE}" scene_entity_destruction_source_text)
    file(READ "${SCENE_STATE_HEADER}" scene_state_header_text)

    foreach(scene_entity_name_cache_required_pattern IN ITEMS
            "std::vector<std::string> denseEntityNames;"
            "std::unordered_map<SceneEntity::IdType, std::string> entityNames;"
            "std::string SceneEntityNaming::Name(const SceneState& state, SceneEntity entity)"
            "return state.world.Name(entity);"
            "if (!world.BackendEntityAlive(entity))"
            "void SceneEntityNaming::SetRepeatedNames(SceneState& state"
            "StoreCachedName(state, entities[index], name);"
            "void SceneEntityNaming::ClearName(SceneState& state, SceneEntity entity) noexcept"
            "SceneEntityNaming::Name(SceneAccess::State(scene), entity)"
            "SceneEntityNaming::SetName(SceneAccess::State(scene), entity, name)"
            "SceneEntityNaming::ClearName(state, entity);")
        string(FIND "${scene_state_header_text}\n${scene_entity_naming_source_text}\n${scene_entity_name_service_source_text}\n${scene_entity_destruction_source_text}" "${scene_entity_name_cache_required_pattern}" scene_entity_name_cache_required_index)
        if(scene_entity_name_cache_required_index EQUAL -1)
            message(FATAL_ERROR "Scene entity names must keep fast scene cache path, missing: ${scene_entity_name_cache_required_pattern}")
        endif()
    endforeach()

    foreach(scene_entity_name_cache_forbidden_pattern IN ITEMS
            "SceneEntityNaming::Name(SceneAccess::State(scene).world"
            "SceneEntityNaming::SetName(SceneAccess::State(scene).world"
            "void SceneEntityNaming::SetRepeatedNames(kb::ecs::World& world"
            "ecs_set_name(world.NativeHandle(), kb::ecs::FlecsEntityId(entities[index])")
        string(FIND "${scene_entity_name_service_source_text}\n${scene_entity_naming_source_text}" "${scene_entity_name_cache_forbidden_pattern}" scene_entity_name_cache_forbidden_index)
        if(NOT scene_entity_name_cache_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene entity repeated names must not use backend per entity in bulk/cache path, found: ${scene_entity_name_cache_forbidden_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED WORLD_ENTITIES_SOURCE AND DEFINED WORLD_ENTITY_CATALOG_SOURCE AND DEFINED WORLD_ENTITY_CATALOG_HEADER AND DEFINED ECS_INSPECTION_TEST_SOURCE)
    file(READ "${WORLD_ENTITIES_SOURCE}" world_entities_source_text)
    file(READ "${WORLD_ENTITY_CATALOG_SOURCE}" world_entity_catalog_source_text)
    file(READ "${WORLD_ENTITY_CATALOG_HEADER}" world_entity_catalog_header_text)
    file(READ "${ECS_INSPECTION_TEST_SOURCE}" ecs_inspection_test_source_text)

    foreach(world_entities_bulk_mirror_pattern IN ITEMS
            "std::vector<std::vector<std::byte>> expandedComponentData;"
            "const std::size_t sourceCount = component.sourceCount == 0U ? entities.size() : component.sourceCount;"
            "const std::size_t sourceIndex = entityIndex % sourceCount;"
            "componentData[index] = expanded.data();")
        string(FIND "${world_entities_source_text}" "${world_entities_bulk_mirror_pattern}" world_entities_bulk_mirror_index)
        if(world_entities_bulk_mirror_index EQUAL -1)
            message(FATAL_ERROR "World backend bulk init must expand broadcast/pattern component columns before mirror write, missing: ${world_entities_bulk_mirror_pattern}")
        endif()
    endforeach()

    foreach(world_entity_catalog_required_pattern IN ITEMS
            "void AddMany(std::span<const Entity> entities);"
            "void RemoveMany(std::span<const Entity> entities);"
            "void WorldEntityCatalog::AddMany(std::span<const Entity> entities)"
            "void WorldEntityCatalog::RemoveMany(std::span<const Entity> entities)"
            "RunEditorBulkCreateCatalogTest"
            "world.CreateEntities(positions.size()")
        string(FIND "${world_entity_catalog_header_text}\n${world_entity_catalog_source_text}\n${ecs_inspection_test_source_text}" "${world_entity_catalog_required_pattern}" world_entity_catalog_required_index)
        if(world_entity_catalog_required_index EQUAL -1)
            message(FATAL_ERROR "World entity catalog must keep bulk add/remove and regression coverage, missing: ${world_entity_catalog_required_pattern}")
        endif()
    endforeach()

    string(FIND "${world_entities_source_text}" "std::vector<Entity> World::CreateEntitiesWithComponents" world_bulk_create_begin)
    string(FIND "${world_entities_source_text}" "void World::AdoptEntitiesWithComponents" world_bulk_create_end)
    if(world_bulk_create_begin EQUAL -1 OR world_bulk_create_end EQUAL -1 OR world_bulk_create_end LESS_EQUAL world_bulk_create_begin)
        message(FATAL_ERROR "World entity catalog guard could not bound CreateEntitiesWithComponents")
    endif()
    math(EXPR world_bulk_create_length "${world_bulk_create_end} - ${world_bulk_create_begin}")
    string(SUBSTRING "${world_entities_source_text}" ${world_bulk_create_begin} ${world_bulk_create_length} world_bulk_create_body)

    string(FIND "${world_entities_source_text}" "void World::AdoptEntitiesWithComponents" world_bulk_adopt_begin)
    string(FIND "${world_entities_source_text}" "void World::DestroyEntity" world_bulk_adopt_end)
    if(world_bulk_adopt_begin EQUAL -1 OR world_bulk_adopt_end EQUAL -1 OR world_bulk_adopt_end LESS_EQUAL world_bulk_adopt_begin)
        message(FATAL_ERROR "World entity catalog guard could not bound AdoptEntitiesWithComponents")
    endif()
    math(EXPR world_bulk_adopt_length "${world_bulk_adopt_end} - ${world_bulk_adopt_begin}")
    string(SUBSTRING "${world_entities_source_text}" ${world_bulk_adopt_begin} ${world_bulk_adopt_length} world_bulk_adopt_body)

    foreach(world_bulk_create_required_pattern IN ITEMS
            "registries_->Entities().AddMany(entities);"
            "registries_->Entities().RemoveMany(entities);")
        string(FIND "${world_bulk_create_body}" "${world_bulk_create_required_pattern}" world_bulk_create_required_index)
        if(world_bulk_create_required_index EQUAL -1)
            message(FATAL_ERROR "World bulk create must update entity catalog in batches, missing: ${world_bulk_create_required_pattern}")
        endif()
    endforeach()

    foreach(world_bulk_adopt_required_pattern IN ITEMS
            "registries_->Entities().AddMany(adoptedEntities);"
            "registries_->Entities().RemoveMany(adoptedEntities);")
        string(FIND "${world_bulk_adopt_body}" "${world_bulk_adopt_required_pattern}" world_bulk_adopt_required_index)
        if(world_bulk_adopt_required_index EQUAL -1)
            message(FATAL_ERROR "World bulk adopt must update entity catalog in batches, missing: ${world_bulk_adopt_required_pattern}")
        endif()
    endforeach()

    foreach(world_bulk_forbidden_pattern IN ITEMS
            "registries_->Entities().Add(entity);"
            "registries_->Entities().Remove(entity);")
        string(FIND "${world_bulk_create_body}\n${world_bulk_adopt_body}" "${world_bulk_forbidden_pattern}" world_bulk_forbidden_index)
        if(NOT world_bulk_forbidden_index EQUAL -1)
            message(FATAL_ERROR "World bulk create/adopt must not update entity catalog per entity, found: ${world_bulk_forbidden_pattern}")
        endif()
    endforeach()
endif()

foreach(prefab_spawn_timing_export_pattern IN ITEMS
        "std::uint64_t prefabSpawnEntityCreateNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandBuildNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandPlaybackNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandPlaybackCreateNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandPlaybackApplyNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandPlaybackParentNanoseconds = 0;"
        "std::uint64_t prefabSpawnCommandPlaybackDestroyNanoseconds = 0;"
        "std::uint64_t prefabSpawnHierarchyRecordNanoseconds = 0;"
        "std::uint64_t prefabSpawnNameAssignmentNanoseconds = 0;"
        "std::size_t prefabSpawnComponentSourceBytesRead = 0;"
        "prefab_spawn_entity_create_ns"
        "prefab_spawn_command_build_ns"
        "prefab_spawn_command_playback_ns"
        "prefab_spawn_command_playback_create_ns"
        "prefab_spawn_command_playback_apply_ns"
        "prefab_spawn_command_playback_parent_ns"
        "prefab_spawn_command_playback_destroy_ns"
        "prefab_spawn_hierarchy_record_ns"
        "prefab_spawn_name_assignment_ns"
        "prefab_spawn_component_source_bytes_read"
        "result.prefabSpawnEntityCreateNanoseconds = stats.entityCreateNanoseconds;"
        "result.prefabSpawnCommandBuildNanoseconds = stats.commandBuildNanoseconds;"
        "result.prefabSpawnCommandPlaybackNanoseconds = stats.commandPlaybackNanoseconds;"
        "result.prefabSpawnCommandPlaybackCreateNanoseconds = stats.commandPlaybackCreateNanoseconds;"
        "result.prefabSpawnCommandPlaybackApplyNanoseconds = stats.commandPlaybackApplyNanoseconds;"
        "result.prefabSpawnCommandPlaybackParentNanoseconds = stats.commandPlaybackParentNanoseconds;"
        "result.prefabSpawnCommandPlaybackDestroyNanoseconds = stats.commandPlaybackDestroyNanoseconds;"
        "result.prefabSpawnHierarchyRecordNanoseconds = stats.hierarchyRecordNanoseconds;"
        "result.prefabSpawnNameAssignmentNanoseconds = stats.nameAssignmentNanoseconds;"
        "result.prefabSpawnComponentSourceBytesRead = stats.componentSourceBytesRead;"
        "result.prefabSpawnComponentSourceBytesRead = measuredStats.prefabSpawnComponentSourceBytesRead;")
    string(FIND "${source_text}" "${prefab_spawn_timing_export_pattern}" prefab_spawn_timing_export_index)
    if(prefab_spawn_timing_export_index EQUAL -1)
        message(FATAL_ERROR "Prefab spawn benchmark telemetry must export create/hierarchy/name timing, missing: ${prefab_spawn_timing_export_pattern}")
    endif()
endforeach()

foreach(prefab_spawn_matrix_contract IN ITEMS
        "std::vector<std::size_t> prefabSpawnInstanceCounts{ 10'000, 50'000, 100'000 };"
        "std::vector<std::size_t> prefabOverrideInstanceCounts{ 1'000, 10'000, 100'000 };"
        "std::vector<std::size_t> prefabHierarchyNodeCounts{ 1, 4, 16 };"
        "results.push_back(RunPrefabSpawnBenchmark(options, instanceCount, nodeCount, false));"
        "results.push_back(RunPrefabSpawnBenchmark(options, instanceCount, nodeCount, true));"
        "results.push_back(RunPrefabRegistryRecordBenchmark(options, instanceCount, nodeCount));"
        "RunPrefabOverrideBenchmark("
        "name/transform/visibility overrides")
    string(FIND "${source_text}" "${prefab_spawn_matrix_contract}" prefab_spawn_matrix_index)
    if(prefab_spawn_matrix_index EQUAL -1)
        message(FATAL_ERROR "Prefab spawn benchmark must keep the full P5 matrix contract, missing: ${prefab_spawn_matrix_contract}")
    endif()
endforeach()

if(DEFINED SCENE_PREFAB_BULK_INSTANTIATION_SOURCE)
    file(READ "${SCENE_PREFAB_BULK_INSTANTIATION_SOURCE}" scene_prefab_bulk_source_text)
    foreach(prefab_spawn_source_bytes_pattern IN ITEMS
            "ComponentSourceBytesRead"
            "AddCommandComponentPatternView"
            "payload.BuildPattern(archetype, instanceCount);"
            "component.sourceCount == 0U ? component.componentCount : component.sourceCount"
            ".componentSourceBytesRead = ComponentSourceBytesRead")
        string(FIND "${scene_prefab_bulk_source_text}" "${prefab_spawn_source_bytes_pattern}" prefab_spawn_source_bytes_index)
        if(prefab_spawn_source_bytes_index EQUAL -1)
            message(FATAL_ERROR "Prefab spawn telemetry must report source component bytes, missing: ${prefab_spawn_source_bytes_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_COMMAND_BUFFER_SOURCE AND DEFINED ECS_COMMAND_BUFFER_TEST_SOURCE)
    file(READ "${ECS_COMMAND_BUFFER_SOURCE}" ecs_command_buffer_source_text)
    file(READ "${ECS_COMMAND_BUFFER_TEST_SOURCE}" ecs_command_buffer_test_text)
    foreach(command_buffer_pattern_bulk_create_pattern IN ITEMS
            "component.sourceCount = componentView.sourceCount == 0U ? componentView.componentCount : componentView.sourceCount;"
            "component.borrowedCount = component.sourceCount;"
            ".componentCount = command.count,"
            ".sourceCount = sourceCount,"
            "stats.componentBytesCopied += component.componentSize * command.count;"
            "RunCommandBufferBorrowedPatternBulkCreateTest")
        string(FIND "${ecs_command_buffer_source_text}\n${ecs_command_buffer_test_text}" "${command_buffer_pattern_bulk_create_pattern}" command_buffer_pattern_bulk_create_index)
        if(command_buffer_pattern_bulk_create_index EQUAL -1)
            message(FATAL_ERROR "Command buffer bulk create must keep sourceCount pattern support, missing: ${command_buffer_pattern_bulk_create_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_PREFAB_INSTANTIATION_TEST_SOURCE)
    file(READ "${SCENE_PREFAB_INSTANTIATION_TEST_SOURCE}" scene_prefab_instantiation_test_text)
    foreach(prefab_spawn_source_bytes_test_pattern IN ITEMS
            "Backend-synced bulk prefab pattern telemetry did not reduce source component bytes"
            "spawnStats.componentSourceBytesRead < spawnStats.componentBytesCopied")
        string(FIND "${scene_prefab_instantiation_test_text}" "${prefab_spawn_source_bytes_test_pattern}" prefab_spawn_source_bytes_test_index)
        if(prefab_spawn_source_bytes_test_index EQUAL -1)
            message(FATAL_ERROR "Prefab spawn tests must validate source component byte telemetry, missing: ${prefab_spawn_source_bytes_test_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_PREFAB_INSTANCE_REGISTRY_SOURCE)
    file(READ "${SCENE_PREFAB_INSTANCE_REGISTRY_SOURCE}" scene_prefab_instance_registry_source_text)
    if(DEFINED SCENE_PREFAB_INSTANCE_REGISTRY_HEADER)
        file(READ "${SCENE_PREFAB_INSTANCE_REGISTRY_HEADER}" scene_prefab_instance_registry_header_text)
    else()
        set(scene_prefab_instance_registry_header_text "")
    endif()
    if(DEFINED SCENE_PREFAB_INSTANCE_HEADER)
        file(READ "${SCENE_PREFAB_INSTANCE_HEADER}" scene_prefab_instance_header_text)
    else()
        set(scene_prefab_instance_header_text "")
    endif()
    if(DEFINED SCENE_PREFAB_SOURCE)
        file(READ "${SCENE_PREFAB_SOURCE}" scene_prefab_source_text)
    else()
        set(scene_prefab_source_text "")
    endif()
    string(FIND "${scene_prefab_instance_registry_source_text}" "std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::RegisterMany" scene_prefab_register_many_begin)
    if(scene_prefab_register_many_begin EQUAL -1)
        message(FATAL_ERROR "Scene prefab instance registry guard did not find RegisterMany")
    endif()
    string(SUBSTRING "${scene_prefab_instance_registry_source_text}" ${scene_prefab_register_many_begin} -1 scene_prefab_register_many_tail)
    string(FIND "${scene_prefab_register_many_tail}" "bool ScenePrefabInstanceRegistry::Contains" scene_prefab_register_many_end)
    if(scene_prefab_register_many_end EQUAL -1)
        message(FATAL_ERROR "Scene prefab instance registry guard could not bound RegisterMany")
    endif()
    string(SUBSTRING "${scene_prefab_register_many_tail}" 0 ${scene_prefab_register_many_end} scene_prefab_register_many_body)
    foreach(scene_prefab_required_pattern IN ITEMS
            "std::make_shared<std::string>(prefabGuid)"
            "std::make_shared<std::vector<std::uint64_t>>(NodeIdsFor(resolvedPrefab))"
            "batchPrefabGuidPool_.push_back(std::move(sharedPrefabGuid));"
            "batchNodeIdPool_.push_back(std::move(sharedNodeIds));"
            "batchResolvedPrefabPool_.push_back(std::move(sharedResolvedPrefab));"
            "const std::string* pooledPrefabGuid = sharedPrefabGuid.get();"
            "const std::vector<std::uint64_t>* pooledNodeIds = sharedNodeIds.get();"
            "const ScenePrefab* pooledResolvedPrefab = nullptr;"
            "ScenePrefabInstanceRecord& record = records_[slot];"
            "record.pooledPrefabGuid = pooledPrefabGuid;"
            "record.pooledNodeIds = pooledNodeIds;"
            "record.pooledResolvedPrefab = pooledResolvedPrefab;")
        string(FIND "${scene_prefab_register_many_body}" "${scene_prefab_required_pattern}" scene_prefab_required_index)
        if(scene_prefab_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab batch registry must keep pooled batch payload pattern: ${scene_prefab_required_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_prefab_register_many_body}" ".prefabGuid = std::string{ prefabGuid }" scene_prefab_guid_copy_index)
    if(NOT scene_prefab_guid_copy_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab batch registry must not allocate a source guid string per instance")
    endif()
    string(FIND "${scene_prefab_register_many_body}" ".nodeIds = resolvedNodeIds" scene_prefab_node_id_copy_index)
    if(NOT scene_prefab_node_id_copy_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab batch registry must not copy stable node ids per instance")
    endif()
    string(FIND "${scene_prefab_register_many_body}" "records_[slot] = ScenePrefabInstanceRecord{" scene_prefab_record_temporary_index)
    if(NOT scene_prefab_record_temporary_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab batch registry must fill fresh record slots directly, not through aggregate record temporaries")
    endif()
    foreach(scene_prefab_instance_registry_required_pattern IN ITEMS
            "std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::RegisterManyInstances"
            "std::span<const ScenePrefabInstance> instances"
            "const std::span<const SceneObject> objects = instance.Objects()"
            "const std::shared_ptr<const std::vector<SceneObject>> sharedObjects = instance.SharedObjects();"
            ".sharedObjects = sharedObjects"
            "const std::span<const std::uint64_t> nodeIds = record.NodeIds()"
            "const std::span<const SceneObject> objects = record.Objects()")
        string(FIND "${scene_prefab_instance_registry_source_text}" "${scene_prefab_instance_registry_required_pattern}" scene_prefab_instance_registry_required_index)
        if(scene_prefab_instance_registry_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab registry must keep direct instance batch registration, missing: ${scene_prefab_instance_registry_required_pattern}")
        endif()
    endforeach()
    foreach(scene_prefab_shared_object_payload_required_pattern IN ITEMS
            "const std::string* pooledPrefabGuid = nullptr;"
            "const std::vector<std::uint64_t>* pooledNodeIds = nullptr;"
            "const ScenePrefab* pooledResolvedPrefab = nullptr;"
            "std::vector<std::shared_ptr<const std::string>> batchPrefabGuidPool_;"
            "std::vector<std::shared_ptr<const std::vector<std::uint64_t>>> batchNodeIdPool_;"
            "std::vector<std::shared_ptr<const ScenePrefab>> batchResolvedPrefabPool_;"
            "std::shared_ptr<const std::vector<SceneObject>> sharedObjects;"
            "std::span<const SceneObject> Objects() const noexcept"
            "std::vector<SceneObject>& MutableObjects()"
            "void SetObjects(std::vector<SceneObject> updatedObjects)"
            "std::shared_ptr<const std::vector<SceneObject>> sharedObjects_;"
            "std::shared_ptr<const std::vector<SceneObject>> ScenePrefabInstance::SharedObjects() const")
        string(FIND "${scene_prefab_instance_registry_header_text}\n${scene_prefab_instance_header_text}\n${scene_prefab_source_text}" "${scene_prefab_shared_object_payload_required_pattern}" scene_prefab_shared_object_payload_required_index)
        if(scene_prefab_shared_object_payload_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab instances must keep shared object payload for registered batch hot path, missing: ${scene_prefab_shared_object_payload_required_pattern}")
        endif()
    endforeach()
    foreach(scene_prefab_dense_record_required_pattern IN ITEMS
            "std::vector<ScenePrefabInstanceRecord> records_;"
            "std::vector<std::uint8_t> recordAlive_;"
            "std::size_t liveRecordCount_ = 0;"
            "std::vector<ObjectIndexEntry> denseObjectIndex_;"
            "void EnsureRecordSlot(ScenePrefabInstanceHandle handle);"
            "void EnsureRecordSlots(std::uint64_t firstId, std::size_t count);"
            "void ScenePrefabInstanceRegistry::EnsureRecordSlots(std::uint64_t firstId, std::size_t count)"
            "std::uint64_t ScenePrefabInstanceRegistry::NodeIdFor(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex) const noexcept"
            "nodeId = NodeIdFor(entry.instance, entry.nodeIndex);"
            "nodeId = NodeIdFor(iterator->second.instance, iterator->second.nodeIndex);"
            "denseObjectIndex_[firstIndex + nodeIndex] = ObjectIndexEntry{"
            "denseObjectIndex_[denseIndex] = ObjectIndexEntry{"
            "const std::uint64_t firstBatchId = nextId_;"
            "EnsureRecordSlots(firstBatchId, objectSets.size());"
            "EnsureRecordSlots(firstBatchId, instances.size());"
            "bool ScenePrefabInstanceRegistry::RecordSlotAlive(ScenePrefabInstanceHandle handle) const noexcept"
            "std::size_t ScenePrefabInstanceRegistry::RecordSlotIndex(ScenePrefabInstanceHandle handle) noexcept"
            "handles.reserve(liveRecordCount_);"
            "return liveRecordCount_;"
            "std::size_t sparseRootCount = 0;"
            "std::size_t sparseObjectCount = 0;"
            "if (sparseRootCount != 0U)"
            "if (sparseObjectCount != 0U)"
            "const bool denseOnlyBatch = sparseRootCount == 0U && sparseObjectCount == 0U;"
            "bool contiguousDenseObjectRuns = true;"
            "const bool contiguousDenseBatch = denseOnlyBatch && contiguousDenseObjectRuns;"
            "void IndexContiguousDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept;"
            "void ScenePrefabInstanceRegistry::IndexContiguousDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept"
            "IndexContiguousDensePreparedObjects(handle, record);"
            "void IndexDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept;"
            "void ScenePrefabInstanceRegistry::IndexDensePreparedObjects(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record) noexcept"
            "IndexDensePreparedObjects(handle, record);")
        string(FIND "${scene_prefab_instance_registry_header_text}\n${scene_prefab_instance_registry_source_text}" "${scene_prefab_dense_record_required_pattern}" scene_prefab_dense_record_required_index)
        if(scene_prefab_dense_record_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab registry must keep dense record/index hot path, missing: ${scene_prefab_dense_record_required_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_prefab_instance_registry_source_text}" "std::vector<SceneObject>{ objects.begin(), objects.end() }" scene_prefab_object_copy_index)
    if(NOT scene_prefab_object_copy_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab RegisterManyInstances must not copy object vectors per instance")
    endif()
    foreach(scene_prefab_dense_record_forbidden_pattern IN ITEMS
            "std::unordered_map<std::uint64_t, ScenePrefabInstanceRecord> records_"
            "records_.emplace("
            "records_.find("
            "records_.erase("
            "records_.contains("
            ".nodeId ="
            "rootIndex_.reserve(rootIndex_.size() + instances.size())"
            "objectIndex_.reserve(objectIndex_.size() + objectCount)"
            "record.sharedPrefabGuid = sharedPrefabGuid;"
            "record.sharedNodeIds = sharedNodeIds;"
            "record.sharedResolvedPrefab = sharedResolvedPrefab;"
            ".sharedPrefabGuid = sharedPrefabGuid"
            ".sharedNodeIds = sharedNodeIds"
            ".sharedResolvedPrefab = sharedResolvedPrefab")
        string(FIND "${scene_prefab_instance_registry_header_text}\n${scene_prefab_instance_registry_source_text}" "${scene_prefab_dense_record_forbidden_pattern}" scene_prefab_dense_record_forbidden_index)
        if(NOT scene_prefab_dense_record_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab registry must not use sparse record/index hot path pattern: ${scene_prefab_dense_record_forbidden_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_PREFAB_REGISTERED_INSTANTIATION_SOURCE)
    file(READ "${SCENE_PREFAB_REGISTERED_INSTANTIATION_SOURCE}" scene_prefab_registered_instantiation_source_text)
    foreach(scene_prefab_registered_required_pattern IN ITEMS
            "state.prefabInstances.RegisterManyInstances"
            "std::span<const ScenePrefabInstance>{ instances }"
            "instances[index].AssignHandle(handles[index]);")
        string(FIND "${scene_prefab_registered_instantiation_source_text}" "${scene_prefab_registered_required_pattern}" scene_prefab_registered_required_index)
        if(scene_prefab_registered_required_index EQUAL -1)
            message(FATAL_ERROR "Registered prefab bulk instantiate must attach handles without rebuilding object vectors, missing: ${scene_prefab_registered_required_pattern}")
        endif()
    endforeach()
    foreach(scene_prefab_registered_forbidden_pattern IN ITEMS
            "TakeObjects()"
            "std::vector<std::vector<SceneObject>> objectSets")
        string(FIND "${scene_prefab_registered_instantiation_source_text}" "${scene_prefab_registered_forbidden_pattern}" scene_prefab_registered_forbidden_index)
        if(NOT scene_prefab_registered_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Registered prefab bulk instantiate must not move objects through temporary objectSets: ${scene_prefab_registered_forbidden_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_HISTORY_SERVICE_SOURCE)
    file(READ "${SCENE_HISTORY_SERVICE_SOURCE}" scene_history_service_source_text)
    string(FIND "${scene_history_service_source_text}" "const ScenePrefab* resolvedPrefab = record->ResolvedPrefab();" scene_history_resolved_accessor_index)
    if(scene_history_resolved_accessor_index EQUAL -1)
        message(FATAL_ERROR "Scene history prefab snapshots must capture resolved prefab through ScenePrefabInstanceRecord::ResolvedPrefab")
    endif()
    string(FIND "${scene_history_service_source_text}" "const std::span<const std::uint64_t> nodeIds = record->NodeIds();" scene_history_node_ids_accessor_index)
    if(scene_history_node_ids_accessor_index EQUAL -1)
        message(FATAL_ERROR "Scene history prefab snapshots must capture stable node ids through ScenePrefabInstanceRecord::NodeIds")
    endif()
    string(FIND "${scene_history_service_source_text}" ".resolvedPrefab = record->resolvedPrefab" scene_history_direct_resolved_index)
    if(NOT scene_history_direct_resolved_index EQUAL -1)
        message(FATAL_ERROR "Scene history prefab snapshots must not ignore shared resolved prefab baselines")
    endif()
    string(FIND "${scene_history_service_source_text}" ".nodeIds = record->nodeIds" scene_history_direct_node_ids_index)
    if(NOT scene_history_direct_node_ids_index EQUAL -1)
        message(FATAL_ERROR "Scene history prefab snapshots must not ignore shared stable node id payloads")
    endif()
endif()

if(DEFINED SCENE_PREFAB_CONNECTION_SOURCE)
    file(READ "${SCENE_PREFAB_CONNECTION_SOURCE}" scene_prefab_connection_source_text)
    foreach(scene_prefab_connection_required_pattern IN ITEMS
            "const ScenePrefab* previousResolved = instance->ResolvedPrefab();"
            "const ScenePrefab previousResolvedPrefab = previousResolved == nullptr ? ScenePrefab{} : *previousResolved;"
            "instance->SetResolvedPrefab(previousResolvedPrefab);")
        string(FIND "${scene_prefab_connection_source_text}" "${scene_prefab_connection_required_pattern}" scene_prefab_connection_required_index)
        if(scene_prefab_connection_required_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab reconnect rollback must preserve shared resolved prefab baseline, missing: ${scene_prefab_connection_required_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_prefab_connection_source_text}" "const ScenePrefab previousResolvedPrefab = instance->resolvedPrefab;" scene_prefab_connection_direct_resolved_index)
    if(NOT scene_prefab_connection_direct_resolved_index EQUAL -1)
        message(FATAL_ERROR "Scene prefab reconnect rollback must not ignore shared resolved prefab baselines")
    endif()
endif()

if(DEFINED SCENE_PREFAB_INSTANTIATION_TEST_SOURCE)
    file(READ "${SCENE_PREFAB_INSTANTIATION_TEST_SOURCE}" scene_prefab_instantiation_test_source_text)
    foreach(scene_prefab_history_contract_pattern IN ITEMS
            "RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest"
            "run(\"RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest\", RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest);"
            "History Bulk Baseline Child Updated"
            "scene.Prefabs().RefreshInstances(prefabHandle) == instances.size()"
            "refresh treated inherited source data as a local override"
            "refresh lost the local transform override")
        string(FIND "${scene_prefab_instantiation_test_source_text}" "${scene_prefab_history_contract_pattern}" scene_prefab_history_contract_index)
        if(scene_prefab_history_contract_index EQUAL -1)
            message(FATAL_ERROR "Scene prefab tests must keep bulk history resolved baseline coverage, missing: ${scene_prefab_history_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_TRANSFORM_ITERATION_SOURCE)
    file(READ "${SCENE_TRANSFORM_ITERATION_SOURCE}" scene_transform_iteration_source_text)
    foreach(scene_transform_iteration_forbidden_pattern IN ITEMS
            "ecs_each_id"
            "SceneComponentIterationAccess::Field<TransformComponent>"
            "SceneComponentIterationAccess::MutableField<TransformComponent>")
        string(FIND "${scene_transform_iteration_source_text}" "${scene_transform_iteration_forbidden_pattern}" scene_transform_iteration_forbidden_index)
        if(NOT scene_transform_iteration_forbidden_index EQUAL -1)
            message(FATAL_ERROR "Scene transform iteration hot path must use typed transform batches, found forbidden pattern: ${scene_transform_iteration_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${scene_transform_iteration_source_text}" "CreateQuery<TransformComponent>" scene_transform_iteration_query_index)
    if(scene_transform_iteration_query_index EQUAL -1)
        message(FATAL_ERROR "Scene transform iteration guard did not find typed transform query")
    endif()
    string(FIND "${scene_transform_iteration_source_text}" "ForEachBatch" scene_transform_iteration_const_batch_index)
    if(scene_transform_iteration_const_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene transform iteration guard did not find const batch iteration")
    endif()
    string(FIND "${scene_transform_iteration_source_text}" "ForEachMutableBatch" scene_transform_iteration_mutable_batch_index)
    if(scene_transform_iteration_mutable_batch_index EQUAL -1)
        message(FATAL_ERROR "Scene transform iteration guard did not find mutable batch iteration")
    endif()
endif()

foreach(scene_root_collector_source_var IN ITEMS SCENE_TRANSFORM_ROOT_COLLECTOR_SOURCE SCENE_HIERARCHY_ROOT_COLLECTOR_SOURCE)
    if(DEFINED ${scene_root_collector_source_var})
        file(READ "${${scene_root_collector_source_var}}" scene_root_collector_source_text)
        foreach(scene_root_collector_forbidden_pattern IN ITEMS
                "ecs_each_id"
                "WorldInternalAccess::ResolveAliveEntity")
            string(FIND "${scene_root_collector_source_text}" "${scene_root_collector_forbidden_pattern}" scene_root_collector_forbidden_index)
            if(NOT scene_root_collector_forbidden_index EQUAL -1)
                message(FATAL_ERROR "Scene root collector hot path must use typed transform batches, found forbidden pattern ${scene_root_collector_forbidden_pattern} in ${scene_root_collector_source_var}")
            endif()
        endforeach()
        string(FIND "${scene_root_collector_source_text}" "CreateQuery<TransformComponent>" scene_root_collector_query_index)
        if(scene_root_collector_query_index EQUAL -1)
            message(FATAL_ERROR "Scene root collector guard did not find typed transform query in ${scene_root_collector_source_var}")
        endif()
        string(FIND "${scene_root_collector_source_text}" "ForEachBatch" scene_root_collector_batch_index)
        if(scene_root_collector_batch_index EQUAL -1)
            message(FATAL_ERROR "Scene root collector guard did not find batch iteration in ${scene_root_collector_source_var}")
        endif()
    endif()
endforeach()

if(DEFINED COMPONENT_PAIR_ITERATION_SOURCE)
    file(READ "${COMPONENT_PAIR_ITERATION_SOURCE}" component_pair_iteration_source_text)
    string(FIND "${component_pair_iteration_source_text}" "ecs_get_id" component_pair_lookup_index)
    if(NOT component_pair_lookup_index EQUAL -1)
        message(FATAL_ERROR "Component pair iteration must read both component columns from a query, found per-entity ecs_get_id")
    endif()
    string(FIND "${component_pair_iteration_source_text}" "ecs_query_iter" component_pair_query_index)
    if(component_pair_query_index EQUAL -1)
        message(FATAL_ERROR "Component pair iteration guard did not find query iteration")
    endif()
    string(FIND "${component_pair_iteration_source_text}" "ecs_field_w_size(&it, static_cast<ecs_size_t>(secondComponentSize), 1)" component_pair_second_field_index)
    if(component_pair_second_field_index EQUAL -1)
        message(FATAL_ERROR "Component pair iteration guard did not find second component column access")
    endif()
endif()

if(DEFINED QUERY_TABLE_BATCH_DISPATCHER_SOURCE)
    file(READ "${QUERY_TABLE_BATCH_DISPATCHER_SOURCE}" query_table_batch_dispatcher_source_text)
    foreach(query_prefetch_dispatch_pattern IN ITEMS
            "#include \"engine/ecs/QueryPrefetch.hpp\""
            "void PrefetchReadOnlyBatch"
            "void PrefetchReadOnlySingleComponent"
            "void PrefetchMutableBatch"
            "void PrefetchMutableSingleComponent"
            "PrefetchQueryMemory(entityIds + prefetchOffset)"
            "PrefetchQueryMemory(bytes + prefetchOffset * componentSizes[field])"
            "PrefetchQueryMemory(componentBytes + prefetchOffset * componentSize)"
            "PrefetchQueryMemory(bytes + prefetchOffset * componentSizes[field], QueryPrefetchAccess::Write)"
            "PrefetchQueryMemory(componentBytes + prefetchOffset * componentSize, QueryPrefetchAccess::Write)"
            "PrefetchReadOnlySingleComponent(entityIds, entityCount, componentBytes, componentSize, offset, prefetchDistance)"
            "PrefetchReadOnlyBatch(entityIds, entityCount, componentSizes, fieldComponents, offset, prefetchDistance)"
            "PrefetchMutableSingleComponent(entityIds, entityCount, componentBytes, componentSize, offset, prefetchDistance)"
            "PrefetchMutableBatch(entityIds, entityCount, componentSizes, fieldComponents, offset, prefetchDistance)")
        string(FIND "${query_table_batch_dispatcher_source_text}" "${query_prefetch_dispatch_pattern}" query_prefetch_dispatch_index)
        if(query_prefetch_dispatch_index EQUAL -1)
            message(FATAL_ERROR "Query table batch dispatcher must keep read/write prefetch hot path, missing: ${query_prefetch_dispatch_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED QUERY_STATE_SOURCE)
    file(READ "${QUERY_STATE_SOURCE}" query_state_source_text)
    foreach(query_state_timing_pattern IN ITEMS
            "ShouldRecordQueryTelemetry"
            "BeginQueryTelemetryTiming"
            "EndQueryTelemetryTiming"
            "std::chrono::steady_clock::now()"
            "settings.telemetryEnabled"
            "counters->queryElapsedNanoseconds += elapsedNanoseconds;"
            "CanExecuteInParallel"
            "ShouldSplitParallelRanges"
            "settings.policy == QueryExecutionPolicy::ParallelRanges || settings.policy == QueryExecutionPolicy::SIMDPreferred"
            "ResolveQueryWorkerCount"
            "scratch.workItems_.push_back(QueryBatchWorkItem{"
            "scratch.chunks_.push_back(WorkerPoolChunk{"
            ".workerCountLimit = queryWorkerCount"
            "settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);"
            "++counters->queryParallelExecutions;"
            "counters->queryWorkerSlots += workerCount;"
            "counters->queryWorkerActiveSlots += std::min(workerCount, parallelWorkItems);"
            "const auto telemetryStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);"
            "EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt)")
        string(FIND "${query_state_source_text}" "${query_state_timing_pattern}" query_state_timing_index)
        if(query_state_timing_index EQUAL -1)
            message(FATAL_ERROR "QueryState telemetry timing contract is missing: ${query_state_timing_pattern}")
        endif()
    endforeach()

    string(FIND "${query_state_source_text}" "settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);" query_state_parallel_dispatch_index)
    string(FIND "${query_state_source_text}" "EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt)," query_state_elapsed_record_index)
    if(query_state_parallel_dispatch_index EQUAL -1 OR query_state_elapsed_record_index EQUAL -1 OR query_state_elapsed_record_index LESS query_state_parallel_dispatch_index)
        message(FATAL_ERROR "QueryState must record elapsed query telemetry after parallel worker dispatch")
    endif()

    foreach(query_state_forbidden_lock_pattern IN ITEMS
            "std::mutex"
            "std::lock_guard"
            "std::unique_lock"
            "std::scoped_lock"
            "std::shared_mutex")
        string(FIND "${query_state_source_text}" "${query_state_forbidden_lock_pattern}" query_state_forbidden_lock_index)
        if(NOT query_state_forbidden_lock_index EQUAL -1)
            message(FATAL_ERROR "QueryState hot path must not introduce global standard locks, found: ${query_state_forbidden_lock_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED QUERY_PREFETCH_HEADER)
    file(READ "${QUERY_PREFETCH_HEADER}" query_prefetch_header_text)
    foreach(query_prefetch_header_pattern IN ITEMS
            "enum class QueryPrefetchAccess"
            "__builtin_prefetch(address, 1, 3)"
            "__builtin_prefetch(address, 0, 3)"
            "_mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0)"
            "PrefetchQueryComponentsAt"
            "PrefetchQueryMemory(pointers == nullptr ? nullptr : static_cast<const void*>(pointers + index), access)")
        string(FIND "${query_prefetch_header_text}" "${query_prefetch_header_pattern}" query_prefetch_header_index)
        if(query_prefetch_header_index EQUAL -1)
            message(FATAL_ERROR "Query prefetch helper must keep platform prefetch dispatch, missing: ${query_prefetch_header_pattern}")
        endif()
    endforeach()
endif()

foreach(generic_component_iteration_source_var IN ITEMS COMPONENT_STORAGE_ITERATION_SOURCE COMPONENT_SNAPSHOT_CAPTURE_SOURCE)
    if(DEFINED ${generic_component_iteration_source_var})
        file(READ "${${generic_component_iteration_source_var}}" generic_component_iteration_source_text)
        string(FIND "${generic_component_iteration_source_text}" "ecs_each_id" generic_component_each_index)
        if(NOT generic_component_each_index EQUAL -1)
            message(FATAL_ERROR "Generic component iteration must use query column iteration, found ecs_each_id in ${generic_component_iteration_source_var}")
        endif()
        string(FIND "${generic_component_iteration_source_text}" "ecs_query_iter" generic_component_query_index)
        if(generic_component_query_index EQUAL -1)
            message(FATAL_ERROR "Generic component iteration guard did not find query iteration in ${generic_component_iteration_source_var}")
        endif()
        string(FIND "${generic_component_iteration_source_text}" "ecs_field_w_size" generic_component_field_index)
        if(generic_component_field_index EQUAL -1)
            message(FATAL_ERROR "Generic component iteration guard did not find column field access in ${generic_component_iteration_source_var}")
        endif()
        string(FIND "${generic_component_iteration_source_text}" "ecs_query_fini" generic_component_query_fini_index)
        if(generic_component_query_fini_index EQUAL -1)
            message(FATAL_ERROR "Generic component iteration guard did not find query cleanup in ${generic_component_iteration_source_var}")
        endif()
    endif()
endforeach()

if(DEFINED COMPONENT_STORAGE_QUERY_SOURCE)
    file(READ "${COMPONENT_STORAGE_QUERY_SOURCE}" component_storage_query_source_text)
    foreach(component_storage_query_forbidden_pattern IN ITEMS
            "return Has(world, entity, componentId) ? ecs_get_id"
            "return Has(world, entity, componentId) ? ecs_get_mut_id")
        string(FIND "${component_storage_query_source_text}" "${component_storage_query_forbidden_pattern}" component_storage_query_forbidden_index)
        if(NOT component_storage_query_forbidden_index EQUAL -1)
            message(FATAL_ERROR "ComponentStorageQuery TryGet must not perform Has()+get double lookup")
        endif()
    endforeach()
    string(FIND "${component_storage_query_source_text}" "ecs_is_alive(world, flecsEntity)" component_storage_query_alive_index)
    if(component_storage_query_alive_index EQUAL -1)
        message(FATAL_ERROR "ComponentStorageQuery guard did not find direct alive validation")
    endif()
endif()

if(DEFINED SCENE_COMPONENT_ACCESS_SOURCE)
    file(READ "${SCENE_COMPONENT_ACCESS_SOURCE}" scene_component_access_source_text)
    string(FIND "${scene_component_access_source_text}" "ecs_is_alive(world, flecsEntity)" scene_component_access_alive_index)
    if(scene_component_access_alive_index EQUAL -1)
        message(FATAL_ERROR "SceneComponentAccess guard did not find direct alive validation")
    endif()
    string(FIND "${scene_component_access_source_text}" "componentId == 0" scene_component_access_component_id_index)
    if(scene_component_access_component_id_index EQUAL -1)
        message(FATAL_ERROR "SceneComponentAccess guard did not find component id validation")
    endif()
endif()

if(DEFINED WORLD_COMPONENTS_SOURCE)
    file(READ "${WORLD_COMPONENTS_SOURCE}" world_components_source_text)
    foreach(world_components_native_only_guard_pattern IN ITEMS
            "const bool backendAlive = BackendEntityAlive(entity);"
            "if (!BackendEntityAlive(entity))"
            "BackendEntityAlive(entity) && WorldComponentReader::Has"
            "BackendEntityAlive(entity) ? WorldComponentReader::TryGet"
            "if (BackendEntityAlive(entity))")
        string(FIND "${world_components_source_text}" "${world_components_native_only_guard_pattern}" world_components_native_only_guard_index)
        if(world_components_native_only_guard_index EQUAL -1)
            message(FATAL_ERROR "World component mutation must guard backend writes for native-only scene entities, missing: ${world_components_native_only_guard_pattern}")
        endif()
    endforeach()
    string(REGEX MATCHALL "records\\.reserve\\(nativeStorage_->ChunkCount\\(\\)\\)" world_components_record_reserves "${world_components_source_text}")
    list(LENGTH world_components_record_reserves world_components_record_reserve_count)
    if(world_components_record_reserve_count LESS 3)
        message(FATAL_ERROR "World raw component iteration must preallocate dispatch records from NativeArchetypeStorage::ChunkCount")
    endif()
    string(FIND "${world_components_source_text}" "NativeEcsStorageStats" world_components_stats_index)
    if(NOT world_components_stats_index EQUAL -1)
        message(FATAL_ERROR "World raw component iteration must not build full storage stats just to reserve dispatch records")
    endif()
endif()

if(DEFINED WORLD_COMPONENT_API_SOURCE)
    file(READ "${WORLD_COMPONENT_API_SOURCE}" world_component_api_source_text)
    foreach(world_component_api_required_pattern IN ITEMS
            "Compatibility/debug iteration for single-component tools and tests"
            "Production"
            "multi-component systems should use Query<T...> batch iteration"
            "chunk hot path"
            "void ForEachMutable(MutableComponentVisitor<T> visitor, void* context);")
        string(FIND "${world_component_api_source_text}" "${world_component_api_required_pattern}" world_component_api_required_index)
        if(world_component_api_required_index EQUAL -1)
            message(FATAL_ERROR "World::ForEachMutable must stay documented as compat/debug API, missing: ${world_component_api_required_pattern}")
        endif()
    endforeach()
    string(FIND "${source_text}" "ForEachMutable<" market_for_each_mutable_index)
    if(NOT market_for_each_mutable_index EQUAL -1)
        message(FATAL_ERROR "Market benchmark source must not use World::ForEachMutable compatibility API")
    endif()
endif()

if(DEFINED ECS_WORLD_RELATIONS_SOURCE AND DEFINED ECS_RELATION_STORAGE_SOURCE AND DEFINED ECS_RELATION_STORAGE_HEADER)
    file(READ "${ECS_WORLD_RELATIONS_SOURCE}" ecs_world_relations_source_text)
    file(READ "${ECS_RELATION_STORAGE_SOURCE}" ecs_relation_storage_source_text)
    file(READ "${ECS_RELATION_STORAGE_HEADER}" ecs_relation_storage_header_text)

    foreach(ecs_relation_storage_required_pattern IN ITEMS
            "AddKnownAlivePairs"
            "std::span<const Entity> entities"
            "std::span<const Entity> targets")
        string(FIND "${ecs_relation_storage_header_text}" "${ecs_relation_storage_required_pattern}" ecs_relation_storage_header_index)
        if(ecs_relation_storage_header_index EQUAL -1)
            message(FATAL_ERROR "ECS relation storage must expose validated bulk pair add path, missing: ${ecs_relation_storage_required_pattern}")
        endif()
    endforeach()

    foreach(ecs_relation_storage_source_required_pattern IN ITEMS
            "std::size_t RelationStorage::AddKnownAlivePairs"
            "ecs_add_pair(world, FlecsEntityId(entity), relation, PairTarget(target))"
            "return added;")
        string(FIND "${ecs_relation_storage_source_text}" "${ecs_relation_storage_source_required_pattern}" ecs_relation_storage_source_index)
        if(ecs_relation_storage_source_index EQUAL -1)
            message(FATAL_ERROR "ECS relation storage validated bulk path missing: ${ecs_relation_storage_source_required_pattern}")
        endif()
    endforeach()

    string(FIND "${ecs_world_relations_source_text}" "void World::SetParentsForNewEntitiesKnownAcyclic" ecs_known_acyclic_parent_begin)
    if(ecs_known_acyclic_parent_begin EQUAL -1)
        message(FATAL_ERROR "ECS relation guard did not find known-acyclic parent batch API")
    endif()
    string(SUBSTRING "${ecs_world_relations_source_text}" ${ecs_known_acyclic_parent_begin} -1 ecs_known_acyclic_parent_tail)
    string(FIND "${ecs_known_acyclic_parent_tail}" "void World::ClearParent" ecs_known_acyclic_parent_end)
    if(ecs_known_acyclic_parent_end EQUAL -1)
        message(FATAL_ERROR "ECS relation guard could not bound known-acyclic parent batch API")
    endif()
    string(SUBSTRING "${ecs_known_acyclic_parent_tail}" 0 ${ecs_known_acyclic_parent_end} ecs_known_acyclic_parent_body)
    foreach(ecs_known_acyclic_parent_required_pattern IN ITEMS
            "RelationStorage::AddKnownAlivePairs(world_, children, EcsChildOf, parents)"
            "InvalidateQueryPlansForArchetypeChange(nullptr, nullptr)")
        string(FIND "${ecs_known_acyclic_parent_body}" "${ecs_known_acyclic_parent_required_pattern}" ecs_known_acyclic_parent_required_index)
        if(ecs_known_acyclic_parent_required_index EQUAL -1)
            message(FATAL_ERROR "Known-acyclic parent batch must use validated bulk relation path, missing: ${ecs_known_acyclic_parent_required_pattern}")
        endif()
    endforeach()
    string(FIND "${ecs_known_acyclic_parent_body}" "RelationStorage::Add(world_" ecs_known_acyclic_parent_slow_index)
    if(NOT ecs_known_acyclic_parent_slow_index EQUAL -1)
        message(FATAL_ERROR "Known-acyclic parent batch must not call per-pair validated RelationStorage::Add")
    endif()
endif()

if(DEFINED ECS_COMMAND_BUFFER_SOURCE)
    file(READ "${ECS_COMMAND_BUFFER_SOURCE}" ecs_command_buffer_source_text)
    string(FIND "${ecs_command_buffer_source_text}" "CommandBufferPlaybackResult CommandBuffer::Playback(World& world)" ecs_command_buffer_full_playback_begin)
    if(ecs_command_buffer_full_playback_begin EQUAL -1)
        message(FATAL_ERROR "ECS command buffer guard did not find full Playback")
    endif()
    string(SUBSTRING "${ecs_command_buffer_source_text}" ${ecs_command_buffer_full_playback_begin} -1 ecs_command_buffer_full_playback_tail)
    string(FIND "${ecs_command_buffer_full_playback_tail}" "CommandBufferPlaybackSlice CommandBuffer::PlaybackSlice" ecs_command_buffer_full_playback_end)
    if(ecs_command_buffer_full_playback_end EQUAL -1)
        message(FATAL_ERROR "ECS command buffer guard could not bound full Playback")
    endif()
    string(SUBSTRING "${ecs_command_buffer_full_playback_tail}" 0 ${ecs_command_buffer_full_playback_end} ecs_command_buffer_full_playback_body)
    foreach(ecs_command_buffer_rollback_pattern IN ITEMS
            "snapshotComponent"
            "rollbackPlayback"
            "world.TryGetComponent"
            "std::vector<Entity> playbackScratchEntities;"
            "std::vector<Entity> playbackScratchParentEntities;"
            "playbackScratchParentEntities.clear();"
            "command.parentBatchKnownAcyclicForNewEntities"
            "world.SetParentsForNewEntitiesKnownAcyclic(playbackScratchEntities, playbackScratchParentEntities)"
            "result.stats_.createPhaseNanoseconds"
            "result.stats_.applyPhaseNanoseconds"
            "result.stats_.parentApplyNanoseconds"
            "result.stats_.destroyPhaseNanoseconds"
            "world.SetParents(playbackScratchEntities, playbackScratchParentEntities)"
            "world.ClearParents(playbackScratchEntities)")
        string(FIND "${ecs_command_buffer_full_playback_body}" "${ecs_command_buffer_rollback_pattern}" ecs_command_buffer_rollback_index)
        if(ecs_command_buffer_rollback_index EQUAL -1)
            message(FATAL_ERROR "ECS command buffer full Playback must retain rollback safety pattern: ${ecs_command_buffer_rollback_pattern}")
        endif()
    endforeach()
    foreach(ecs_command_buffer_full_forbidden_pattern IN ITEMS
            "std::vector<Entity> children;"
            "std::vector<Entity> parents;")
        string(FIND "${ecs_command_buffer_full_playback_body}" "${ecs_command_buffer_full_forbidden_pattern}" ecs_command_buffer_full_forbidden_index)
        if(NOT ecs_command_buffer_full_forbidden_index EQUAL -1)
            message(FATAL_ERROR "ECS command buffer full Playback parent bulk path must reuse scratch vectors, found: ${ecs_command_buffer_full_forbidden_pattern}")
        endif()
    endforeach()

    string(FIND "${ecs_command_buffer_source_text}" "CommandBufferPlaybackSlice CommandBuffer::PlaybackSlice" ecs_command_buffer_slice_begin)
    if(ecs_command_buffer_slice_begin EQUAL -1)
        message(FATAL_ERROR "ECS command buffer guard did not find PlaybackSlice")
    endif()
    string(SUBSTRING "${ecs_command_buffer_source_text}" ${ecs_command_buffer_slice_begin} -1 ecs_command_buffer_slice_tail)
    string(FIND "${ecs_command_buffer_slice_tail}" "void CommandBuffer::Clear" ecs_command_buffer_slice_end)
    if(ecs_command_buffer_slice_end EQUAL -1)
        message(FATAL_ERROR "ECS command buffer guard could not bound PlaybackSlice")
    endif()
    string(SUBSTRING "${ecs_command_buffer_slice_tail}" 0 ${ecs_command_buffer_slice_end} ecs_command_buffer_slice_body)
    foreach(ecs_command_buffer_slice_forbidden_pattern IN ITEMS
            "world.TryGetComponent"
            "snapshotComponent"
            "componentRollback"
            "parentRollback"
            "rollbackPlayback"
            "std::vector<Entity> entities;"
            "std::vector<Entity> children;"
            "std::vector<Entity> parents;"
            "std::vector<World::BulkComponentData> components;"
            "std::vector<ComponentId> componentIds;")
        string(FIND "${ecs_command_buffer_slice_body}" "${ecs_command_buffer_slice_forbidden_pattern}" ecs_command_buffer_slice_forbidden_index)
        if(NOT ecs_command_buffer_slice_forbidden_index EQUAL -1)
            message(FATAL_ERROR "ECS command buffer sliced playback must stay on the no-rollback fast path, found: ${ecs_command_buffer_slice_forbidden_pattern}")
        endif()
    endforeach()
    foreach(ecs_command_buffer_slice_scratch_pattern IN ITEMS
            "state.scratchEntities_.clear();"
            "state.scratchParentEntities_.clear();"
            "state.scratchComponentData_.clear();"
            "state.scratchComponentIds_.clear();"
            "world.AddComponents(state.scratchEntities_, state.scratchComponentData_)"
            "world.RemoveComponents(state.scratchEntities_, state.scratchComponentIds_)"
            "if (command.kind == CommandKind::SetParents)"
            "if (command.kind == CommandKind::ClearParents)"
            "command.parentBatchKnownAcyclicForNewEntities"
            "world.SetParentsForNewEntitiesKnownAcyclic(state.scratchEntities_, state.scratchParentEntities_)"
            "world.SetParents(state.scratchEntities_, state.scratchParentEntities_)"
            "world.ClearParents(state.scratchEntities_)")
        string(FIND "${ecs_command_buffer_slice_body}" "${ecs_command_buffer_slice_scratch_pattern}" ecs_command_buffer_slice_scratch_index)
        if(ecs_command_buffer_slice_scratch_index EQUAL -1)
            message(FATAL_ERROR "ECS command buffer sliced playback must reuse playback-state scratch buffers, missing: ${ecs_command_buffer_slice_scratch_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED SCENE_PREFAB_BULK_INSTANTIATION_SOURCE)
    file(READ "${SCENE_PREFAB_BULK_INSTANTIATION_SOURCE}" scene_prefab_bulk_instantiation_source_text)
    string(FIND "${scene_prefab_bulk_instantiation_source_text}" "worker.SetParentsForNewEntitiesKnownAcyclic" prefab_known_acyclic_parent_index)
    if(prefab_known_acyclic_parent_index EQUAL -1)
        message(FATAL_ERROR "Prefab world-hierarchy bulk path must use known-acyclic new-entity parent command buffer path")
    endif()
endif()

if(DEFINED ECS_COMMAND_BUFFER_TEST_SOURCE)
    file(READ "${ECS_COMMAND_BUFFER_TEST_SOURCE}" ecs_command_buffer_test_source_text)
    foreach(ecs_command_buffer_known_acyclic_test_pattern IN ITEMS
            "RunCommandBufferKnownAcyclicNewEntityParentChangesTest"
            "SetParentsForNewEntitiesKnownAcyclic"
            "RunCommandBufferRandomStructuralChangeStressTest"
            "RunCommandBufferRandomStructuralChangeStressTest();"
            "Random structural stress native storage live count diverged")
        string(FIND "${ecs_command_buffer_test_source_text}" "${ecs_command_buffer_known_acyclic_test_pattern}" ecs_command_buffer_known_acyclic_test_index)
        if(ecs_command_buffer_known_acyclic_test_index EQUAL -1)
            message(FATAL_ERROR "ECS command buffer tests must cover known-acyclic new-entity parent batches, missing: ${ecs_command_buffer_known_acyclic_test_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_CONFIG_TEST_SOURCE)
    file(READ "${ECS_CONFIG_TEST_SOURCE}" ecs_config_test_source_text)
    foreach(ecs_config_contract_pattern IN ITEMS
            "kMinChunkPayloadBytes == 4 * 1024"
            "kMaxChunkPayloadBytes == 512 * 1024"
            "IsPowerOfTwoChunkPayloadBytes(4 * 1024)"
            "!kb::ecs::IsPowerOfTwoChunkPayloadBytes(12 * 1024)"
            "IsValidCustomChunkPayloadBytes(4 * 1024)"
            "IsValidCustomChunkPayloadBytes(512 * 1024)"
            "!kb::ecs::IsValidCustomChunkPayloadBytes(2 * 1024)"
            "!kb::ecs::IsValidCustomChunkPayloadBytes(12 * 1024)"
            "!kb::ecs::IsValidCustomChunkPayloadBytes(1024 * 1024)"
            "ChunkSizeProfileFromPayloadBytes(4 * 1024) == kb::ecs::ChunkSizeProfile::Chunk4KB"
            "ChunkSizeProfileFromPayloadBytes(512 * 1024) == kb::ecs::ChunkSizeProfile::Chunk512KB"
            "!kb::ecs::ChunkSizeProfileFromPayloadBytes(12 * 1024).has_value()"
            "ParseChunkSizeProfileWithDiagnostics(\"4kb\").HasValue()"
            "ParseChunkSizeProfileWithDiagnostics(\"\").error == kb::ecs::ChunkSizeProfileParseError::Empty"
            "ParseChunkSizeProfileWithDiagnostics(\"12KB\").error == kb::ecs::ChunkSizeProfileParseError::UnsupportedValue"
            "QueryExecutionPolicyName(kb::ecs::QueryExecutionPolicy::SIMDPreferred) == \"simd_preferred\""
            "ParseQueryExecutionPolicy(\"parallel_chunks\") == kb::ecs::QueryExecutionPolicy::ParallelChunks"
            "ParseQueryExecutionPolicyWithDiagnostics(\"deterministic\").policy == kb::ecs::QueryExecutionPolicy::Deterministic"
            "ParseQueryExecutionPolicyWithDiagnostics(\"\").error == kb::ecs::QueryExecutionPolicyParseError::Empty"
            "ParseQueryExecutionPolicyWithDiagnostics(\"workers\").error == kb::ecs::QueryExecutionPolicyParseError::UnsupportedValue"
            "WorldProfileName(kb::ecs::WorldProfile::Desktop64K) == \"desktop64k\""
            "ParseWorldProfileWithDiagnostics(\"mobile-4k\").profile == kb::ecs::WorldProfile::Mobile4K"
            "ParseWorldProfileWithDiagnostics(\"benchmark_auto\").profile == kb::ecs::WorldProfile::BenchmarkAuto"
            "ParseWorldProfileWithDiagnostics(\"\").error == kb::ecs::WorldProfileParseError::Empty"
            "ParseWorldProfileWithDiagnostics(\"desktop128k\").error == kb::ecs::WorldProfileParseError::UnsupportedValue"
            "WorldProfileConfig(kb::ecs::WorldProfile::Streaming128KPlus).chunkSizeProfile == kb::ecs::ChunkSizeProfile::Chunk128KB")
        string(FIND "${ecs_config_test_source_text}" "${ecs_config_contract_pattern}" ecs_config_contract_index)
        if(ecs_config_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS config tests must keep chunk payload validation coverage, missing: ${ecs_config_contract_pattern}")
        endif()
    endforeach()
endif()

foreach(chunk_parser_contract IN ITEMS
        "ChunkSizeProfileParseError"
        "ChunkSizeProfileParseResult"
        "ParseChunkSizeProfileWithDiagnostics"
        "chunk size is empty"
        "unsupported chunk size")
    string(FIND "${source_text}" "${chunk_parser_contract}" chunk_parser_source_index)
    if(chunk_parser_source_index EQUAL -1)
        if(DEFINED ECS_CHUNK_SIZE_PROFILE_HEADER)
            file(READ "${ECS_CHUNK_SIZE_PROFILE_HEADER}" ecs_chunk_size_profile_text)
            string(FIND "${ecs_chunk_size_profile_text}" "${chunk_parser_contract}" chunk_parser_header_index)
            if(chunk_parser_header_index EQUAL -1)
                message(FATAL_ERROR "ECS chunk size parser must keep diagnostic parsing contract, missing: ${chunk_parser_contract}")
            endif()
        else()
            message(FATAL_ERROR "ECS chunk size parser guard needs ECS_CHUNK_SIZE_PROFILE_HEADER for: ${chunk_parser_contract}")
        endif()
    endif()
endforeach()

foreach(query_execution_policy_contract IN ITEMS
        "QueryExecutionPolicyParseError"
        "QueryExecutionPolicyParseResult"
        "QueryExecutionPolicyName"
        "ParseQueryExecutionPolicyWithDiagnostics"
        "unsupported query execution policy")
    string(FIND "${source_text}" "${query_execution_policy_contract}" query_execution_policy_source_index)
    if(query_execution_policy_source_index EQUAL -1)
        if(DEFINED ECS_QUERY_EXECUTION_SETTINGS_HEADER)
            file(READ "${ECS_QUERY_EXECUTION_SETTINGS_HEADER}" ecs_query_execution_settings_text)
            string(FIND "${ecs_query_execution_settings_text}" "${query_execution_policy_contract}" query_execution_policy_header_index)
            if(query_execution_policy_header_index EQUAL -1)
                message(FATAL_ERROR "ECS query execution policy parser must keep diagnostic parsing contract, missing: ${query_execution_policy_contract}")
            endif()
        else()
            message(FATAL_ERROR "ECS query execution policy guard needs ECS_QUERY_EXECUTION_SETTINGS_HEADER for: ${query_execution_policy_contract}")
        endif()
    endif()
endforeach()

foreach(world_profile_contract IN ITEMS
        "WorldProfileParseError"
        "WorldProfileParseResult"
        "WorldProfileName"
        "WorldProfileConfig"
        "ParseWorldProfileWithDiagnostics")
    string(FIND "${source_text}" "${world_profile_contract}" world_profile_source_index)
    if(world_profile_source_index EQUAL -1)
        if(DEFINED ECS_WORLD_PROFILE_HEADER)
            file(READ "${ECS_WORLD_PROFILE_HEADER}" ecs_world_profile_text)
            string(FIND "${ecs_world_profile_text}" "${world_profile_contract}" world_profile_header_index)
            if(world_profile_header_index EQUAL -1)
                message(FATAL_ERROR "ECS world profile parser must keep public parsing/config contract, missing: ${world_profile_contract}")
            endif()
        else()
            message(FATAL_ERROR "ECS world profile guard needs ECS_WORLD_PROFILE_HEADER for: ${world_profile_contract}")
        endif()
    endif()
endforeach()

if(DEFINED ECS_KERNEL_TEST_SOURCE)
    file(READ "${ECS_KERNEL_TEST_SOURCE}" ecs_kernel_test_source_text)
    foreach(ecs_kernel_contract_pattern IN ITEMS
            "RunEcsKernelRequestedSimdFallsBackToScalarTest"
            "RunEcsKernelSse2AndAvx2DispatchTest"
            "RunEcsKernelNeonDispatchTest"
            "RunEcsKernelAvx512DispatchTest"
            "RunEcsKernelScalarSimdCompatibilityTest"
            "RunEcsKernelVectorMathUnalignedFallbackTest"
            "RunEcsKernelBackendReportNamesTest"
            "RunEcsPreferredKernelBackendReportTest"
            "KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Scalar) == 1U"
            "KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Neon) == 4U"
            "KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Sse2) == 4U"
            "KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx2) == 8U"
            "KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx512) == 16U"
            "RunEcsKernelRequestedSimdFallsBackToScalarTest();"
            "RunEcsKernelSse2AndAvx2DispatchTest();"
            "RunEcsKernelNeonDispatchTest();"
            "RunEcsKernelAvx512DispatchTest();"
            "RunEcsKernelScalarSimdCompatibilityTest();"
            "RunEcsKernelVectorMathUnalignedFallbackTest();"
            "RunEcsKernelBackendReportNamesTest();"
            "RunEcsPreferredKernelBackendReportTest();")
        string(FIND "${ecs_kernel_test_source_text}" "${ecs_kernel_contract_pattern}" ecs_kernel_contract_index)
        if(ecs_kernel_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS kernel tests must keep SIMD dispatch/report coverage, missing: ${ecs_kernel_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED BENCHMARK_CMAKE_SOURCE)
    file(READ "${BENCHMARK_CMAKE_SOURCE}" benchmark_cmake_source_text)
    foreach(benchmark_cmake_required_pattern IN ITEMS
            "kb_ecs_benchmark_rejects_invalid_chunk_size"
            "--chunk-size 12KB"
            "WILL_FAIL TRUE"
            "telemetry_compat_mutable_iterations"
            "telemetry_compat_mutable_entities_visited"
            "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
            "kb_ecs_market_runner_transform_no_hierarchy_summary"
            "kb_ecs_market_runner_scene_render_sync_summary"
            "kb_ecs_market_runner_query_fanout_summary"
            "kb_ecs_market_runner_sparse_query_summary"
            "kb_ecs_market_runner_hierarchy_summary"
            "kb_ecs_market_full_scale_chunk_sweep"
            "-Scales 10000,100000,1000000,10000000,100000000"
            "EcsMarketFullScaleChunkSweep"
            "-ChunkSweep"
            "-BenchmarkCategories raw_kernel,ecs_chunk_iteration")
        string(FIND "${benchmark_cmake_source_text}" "${benchmark_cmake_required_pattern}" benchmark_cmake_required_index)
        if(benchmark_cmake_required_index EQUAL -1)
            message(FATAL_ERROR "Full-scale chunk sweep target must keep required benchmark matrix pattern: ${benchmark_cmake_required_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED MARKET_SUMMARY_ASSERT_SOURCE)
    file(READ "${MARKET_SUMMARY_ASSERT_SOURCE}" market_summary_assert_text)
    foreach(market_summary_assert_contract IN ITEMS
            "REQUIRED_ZERO_COLUMNS"
            "_kb_required_zero_columns"
            "must be zero")
        string(FIND "${market_summary_assert_text}" "${market_summary_assert_contract}" market_summary_assert_contract_index)
        if(market_summary_assert_contract_index EQUAL -1)
            message(FATAL_ERROR "Market summary assertion must support zero-valued telemetry guards, missing: ${market_summary_assert_contract}")
        endif()
    endforeach()
endif()

foreach(scene_light_source_var IN ITEMS SCENE_CAMERA_SYNC_SOURCE SCENE_LIGHT_SYNC_SOURCE)
    if(DEFINED ${scene_light_source_var})
        file(READ "${${scene_light_source_var}}" scene_light_source_text)
        string(FIND "${scene_light_source_text}" "SceneComponentIterationAccess::TryGet" scene_light_lookup_index)
        if(NOT scene_light_lookup_index EQUAL -1)
            message(FATAL_ERROR "Scene camera/light sync hot path must iterate component fields, found per-entity TryGet in ${scene_light_source_var}")
        endif()
        string(FIND "${scene_light_source_text}" "ecs_query_iter" scene_light_query_index)
        if(scene_light_query_index EQUAL -1)
            message(FATAL_ERROR "Scene camera/light sync hot path guard did not find query iteration in ${scene_light_source_var}")
        endif()
        string(FIND "${scene_light_source_text}" "cachedQuery" scene_light_cached_query_index)
        if(scene_light_cached_query_index EQUAL -1)
            message(FATAL_ERROR "Scene camera/light sync hot path guard did not find cached query reuse in ${scene_light_source_var}")
        endif()
        string(FIND "${scene_light_source_text}" "SceneComponentIterationAccess::Field<TransformComponent>" scene_light_transform_field_index)
        if(scene_light_transform_field_index EQUAL -1)
            message(FATAL_ERROR "Scene camera/light sync hot path guard did not find transform column access in ${scene_light_source_var}")
        endif()
    endif()
endforeach()

if(DEFINED DETERMINISTIC_REPLAY_SOURCE)
    file(READ "${DETERMINISTIC_REPLAY_SOURCE}" deterministic_replay_source_text)
    foreach(deterministic_forbidden_pattern IN ITEMS "ForEachMutable<" "TryGet<")
        string(FIND "${deterministic_replay_source_text}" "${deterministic_forbidden_pattern}" deterministic_forbidden_index)
        if(NOT deterministic_forbidden_index EQUAL -1)
            message(FATAL_ERROR "ECS deterministic replay must exercise batch query execution, found forbidden pattern: ${deterministic_forbidden_pattern}")
        endif()
    endforeach()
    string(FIND "${deterministic_replay_source_text}" "ForEachMutableBatchKernel" deterministic_batch_index)
    if(deterministic_batch_index EQUAL -1)
        message(FATAL_ERROR "ECS deterministic replay guard did not find mutable batch query execution")
    endif()
endif()

if(DEFINED ECS_QUERY_TEST_SOURCE)
    file(READ "${ECS_QUERY_TEST_SOURCE}" ecs_query_test_source_text)
    foreach(ecs_query_contract_pattern IN ITEMS
            "RunTypedEcsQueryBatchPointerContractTest"
            "RunTypedEcsQueryBatchAlignmentContractTest"
            "RunTypedEcsQueryPerWorkerReductionScratchTest"
            "RunTypedEcsQueryPerWorkerReductionScratchHonorsWorkerOverrideTest"
            "RunTypedEcsMutableQueryBatchMatchesReferenceTest"
            "RunTypedEcsQueryBatchPointerContractTest();"
            "RunTypedEcsQueryBatchAlignmentContractTest();"
            "RunTypedEcsQueryPerWorkerReductionScratchTest();"
            "RunTypedEcsQueryPerWorkerReductionScratchHonorsWorkerOverrideTest();"
            "RunTypedEcsMutableQueryBatchMatchesReferenceTest();"
            "positionRef == positions + row"
            "velocityRef == velocities + row"
            "IsAlignedAddress(positions + row"
            "IsAlignedAddress(payloads + row"
            "referenceWorld.TryGetMutable<EcsPosition>(entity)"
            "referenceWorld.TryGet<EcsVelocity>(entity)"
            "batchCounters.visited == referenceVisited"
            "Mutable ECS batch query diverged from reference position x"
            "QueryReductionScratch<QueryReductionScratchSlot>"
            "SlotForCurrentWorker"
            ".reductionMode = kb::ecs::QueryReductionMode::PerWorker"
            ".workerCountOverride = 2")
        string(FIND "${ecs_query_test_source_text}" "${ecs_query_contract_pattern}" ecs_query_contract_index)
        if(ecs_query_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS query tests must keep batch pointer/alignment/reduction coverage, missing: ${ecs_query_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_INSPECTION_TEST_SOURCE)
    file(READ "${ECS_INSPECTION_TEST_SOURCE}" ecs_inspection_test_source_text)
    foreach(ecs_inspection_contract_pattern IN ITEMS
            "RunWorldTelemetrySnapshotTest"
            "RunWorldTelemetrySnapshotTest();"
            "WorldTelemetrySnapshot emptySnapshot"
            "WorldTelemetrySnapshot snapshot"
            "queryPlanRequests == 2"
            "queryCacheHits == 1"
            "queryCacheMisses == 1"
            "queryCacheHitPercent"
            "queryCacheMissPercent"
            "queryExecutions == 1"
            "queryBatches == 1"
            "queryEntitiesVisited == 1"
            "queryBytesTouched == sizeof(EcsPosition) + sizeof(EcsVelocity)"
            "queryElapsedNanoseconds > 0"
            "queryEstimatedBytesPerSecond > 0.0"
            "queryParallelExecutions == 1"
            "queryWorkerSlots == 1"
            "queryWorkerActiveSlots == 1"
            "queryWorkerUtilizationPercent"
            "compatMutableIterations == 1"
            "compatMutableEntitiesVisited == 1"
            "structuralChangesSinceReset == 3"
            "totalStructuralChanges == 3"
            "world.ResetTelemetryFrameCounters();"
            "resetSnapshot.structuralChangesSinceReset == 0"
            "resetSnapshot.queryExecutions == 0"
            "resetSnapshot.queryElapsedNanoseconds == 0"
            "resetSnapshot.queryEstimatedBytesPerSecond == 0.0"
            "resetSnapshot.compatMutableIterations == 0"
            "resetSnapshot.compatMutableEntitiesVisited == 0"
            "resetSnapshot.totalStructuralChanges == 3"
            "occupancyPercent + snapshot.fragmentationPercent")
        string(FIND "${ecs_inspection_test_source_text}" "${ecs_inspection_contract_pattern}" ecs_inspection_contract_index)
        if(ecs_inspection_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS inspection tests must keep runtime telemetry snapshot coverage, missing: ${ecs_inspection_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_WORKER_POOL_TEST_SOURCE)
    file(READ "${ECS_WORKER_POOL_TEST_SOURCE}" ecs_worker_pool_test_source_text)
    foreach(ecs_worker_pool_contract_pattern IN ITEMS
            "RunWorkerPoolPropagatesJobExceptionTest"
            "RunWorkerPoolCancelsPendingJobsAfterExceptionTest"
            "RunWorkerPoolCancelsPendingChunksAfterExceptionTest"
            "RunWorkerPoolStopCancelsSubmittedHandleTest"
            "RunWorkerPoolJobFenceWaitsForAllHandlesBeforeRethrowTest"
            "RunWorkerPoolSubmittedWorkHonorsWorkerCountLimitTest"
            "RunWorkerPoolPropagatesJobExceptionTest();"
            "RunWorkerPoolCancelsPendingJobsAfterExceptionTest();"
            "RunWorkerPoolCancelsPendingChunksAfterExceptionTest();"
            "RunWorkerPoolStopCancelsSubmittedHandleTest();"
            "RunWorkerPoolJobFenceWaitsForAllHandlesBeforeRethrowTest();"
            "RunWorkerPoolSubmittedWorkHonorsWorkerCountLimitTest();")
        string(FIND "${ecs_worker_pool_test_source_text}" "${ecs_worker_pool_contract_pattern}" ecs_worker_pool_contract_index)
        if(ecs_worker_pool_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS worker pool tests must keep cancellation/error/worker-limit coverage, missing: ${ecs_worker_pool_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_RELATION_TEST_SOURCE)
    file(READ "${ECS_RELATION_TEST_SOURCE}" ecs_relation_test_source_text)
    foreach(ecs_relation_contract_pattern IN ITEMS
            "RunBatchParentHierarchyApiTest"
            "RunBatchParentTelemetryTest"
            "world.SetParents(std::span<const kb::ecs::Entity>{ children }, std::span<const kb::ecs::Entity>{ parents })"
            "world.ClearParents(std::span<const kb::ecs::Entity>{ children })"
            "ECS batch parent accepted a cycle edge"
            "setSnapshot.structuralChangesSinceReset == 1U"
            "clearSnapshot.structuralChangesSinceReset == 1U"
            "unchangedSnapshot.structuralChangesSinceReset == 0U"
            "RunBatchParentHierarchyApiTest();"
            "RunBatchParentTelemetryTest();")
        string(FIND "${ecs_relation_test_source_text}" "${ecs_relation_contract_pattern}" ecs_relation_contract_index)
        if(ecs_relation_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS relation tests must keep batch parent API coverage, missing: ${ecs_relation_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_NATIVE_STORAGE_TEST_SOURCE)
    file(READ "${ECS_NATIVE_STORAGE_TEST_SOURCE}" ecs_native_storage_test_source_text)
    foreach(ecs_native_storage_contract_pattern IN ITEMS
            "RunChunkPoolReuseAccountingTest"
            "RunChunkPoolMaintenanceBudgetTest"
            "RunMultiComponentMigrationTest"
            "RunBulkRemoveComponentMigrationTest"
            "RunMoveLastCompactionAcrossChunksTest"
            "RunBulkStructuralScratchReuseTest"
            "RunWorldNativeStorageMaintenanceTest"
            "RunWorldBulkStorageStatsConsistencyTest"
            "RunWorldBulkMappingIntegrityTest"
            "RunWorldBulkVersioningTest"
            "RequireStorageStatsConsistent"
            "stats.fragmentedChunks == 0U"
            "stats.emptyChunks == 0U"
            "stats.chunkPoolAllocated == stats.chunkPoolInUse + stats.chunkPoolFree"
            "afterReuse.chunkPoolReuseCount == 1U"
            "storage.Stats().chunkPoolTrimCount == first.chunksReleasedToSystem + second.chunksReleasedToSystem"
            "storage.ChunkCount() == afterReuse.chunks"
            "chunksReleasedToSystem == 2U"
            "storage.AddComponents(std::span<const kb::ecs::Entity>{ evenEntities }, velocityColumn)"
            "storage.RemoveComponents(std::span<const kb::ecs::Entity>{ migrated }, std::span<const kb::ecs::ComponentId>{ removed })"
            "records.front().entityIds != nullptr && records.front().entityIds[0] == last.Id()"
            "RunChunkPoolReuseAccountingTest();"
            "RunChunkPoolMaintenanceBudgetTest();"
            "RunMultiComponentMigrationTest();"
            "RunBulkRemoveComponentMigrationTest();"
            "RunMoveLastCompactionAcrossChunksTest();"
            "RunBulkStructuralScratchReuseTest();"
            "RunWorldNativeStorageMaintenanceTest();"
            "RunWorldBulkStorageStatsConsistencyTest();"
            "RunWorldBulkMappingIntegrityTest();"
            "RunWorldBulkVersioningTest();")
        string(FIND "${ecs_native_storage_test_source_text}" "${ecs_native_storage_contract_pattern}" ecs_native_storage_contract_index)
        if(ecs_native_storage_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS native storage tests must keep compaction/maintenance/bulk mapping coverage, missing: ${ecs_native_storage_contract_pattern}")
        endif()
    endforeach()
endif()

if(DEFINED ECS_NATIVE_STORAGE_SOURCE)
    file(READ "${ECS_NATIVE_STORAGE_SOURCE}" ecs_native_storage_source_text)
    foreach(ecs_native_storage_scratch_contract_pattern IN ITEMS
            "uniqueIdsScratch_"
            "recordIndicesScratch_"
            "destroyLocationsByTableScratch_"
            "migrationGroupsScratch_"
            "appendLocationsScratch_"
            "targetLocationsScratch_"
            "movedEntitiesScratch_"
            "removedRowsScratch_"
            "targetTypesScratch_"
            "ResetUniqueIds"
            "ResetDestroyLocationGroups"
            "ResetMigrationGroups"
            "ResetTargetTypesFrom"
            "tables_[sourceIndex].RemoveMany(group.sourceLocations, movedEntitiesScratch_, removedRowsScratch_);"
            "tables_[tableIndex].RemoveMany(locations, movedEntitiesScratch_, removedRowsScratch_);"
            "void AddMany(std::span<const Entity> entities, std::size_t tableIndex, std::vector<EntityLocation>& locations, bool clearRows = true)"
            "if (clearRows) {"
            "records_.reserve(records_.size() + count);"
            "AppendEntitiesToTable(tableIndex, entities, components, true);"
            "CaptureChunkedSnapshot(std::span<const ComponentTypeInfo> componentTypes, ChunkedWorldSnapshot& snapshot)"
            "CaptureChunkedDeltaSnapshot("
            "StreamChunkedSnapshot("
            "ChunkedWorldSnapshotChunkVisitor visitor"
            "stats.chunkPoolAllocated = pool_.AllocatedChunks();"
            "stats.chunkPoolReuseCount = pool_.ReuseCount();"
            "stats.chunkPoolTrimCount = pool_.TrimCount();"
            "bool generatedOwnedEntities = false"
            "if (generatedOwnedEntities) {"
            "target.AddMany(group.entities, targetIndex, targetLocations, false);"
            "table.AddMany(entities, tableIndex, locations, false);"
            "WriteRepeatedComponentRows(destination, source, column->type.size, count);"
            "static void WriteRepeatedComponentRows(std::byte* destination, const std::byte* source, std::size_t componentSize, std::size_t rowCount)")
        string(FIND "${ecs_native_storage_source_text}" "${ecs_native_storage_scratch_contract_pattern}" ecs_native_storage_scratch_contract_index)
        if(ecs_native_storage_scratch_contract_index EQUAL -1)
            message(FATAL_ERROR "ECS native storage bulk structural path must reuse scratch buffers, missing: ${ecs_native_storage_scratch_contract_pattern}")
        endif()
    endforeach()

    foreach(ecs_native_storage_forbidden_pattern IN ITEMS
            "std::unordered_map<std::size_t, BulkMigrationGroup> groups;"
            "std::unordered_map<std::size_t, std::vector<EntityLocation>> locationsByTable;"
            "std::unordered_set<Entity::IdType> uniqueIds;"
            "std::vector<std::pair<Entity, EntityLocation>> movedEntities ="
            "std::vector<EntityLocation> targetLocations;"
            "std::vector<EntityLocation> locations;"
            "std::vector<Entity> adopted;"
            "std::vector<std::size_t> removedRows;"
            "for (std::size_t row = 0; row < count; ++row) {\n                std::memcpy(destination + (row * column->type.size), source, column->type.size);")
        string(FIND "${ecs_native_storage_source_text}" "${ecs_native_storage_forbidden_pattern}" ecs_native_storage_forbidden_index)
        if(NOT ecs_native_storage_forbidden_index EQUAL -1)
            message(FATAL_ERROR "ECS native storage bulk structural path must not allocate local hot-path containers, found: ${ecs_native_storage_forbidden_pattern}")
        endif()
    endforeach()
endif()
