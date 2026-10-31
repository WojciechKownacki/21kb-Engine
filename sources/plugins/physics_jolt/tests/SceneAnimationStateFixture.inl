struct SceneAnimationStateFixture {
    kb::scene::Scene& scene;
    kb::scene::SceneObject animated;
    kb::scene::SceneObject sequenced;
    std::uint64_t timeline = 0U;
    kb::assets::AssetId controller;

    static void Write(const std::filesystem::path& root) {
        using namespace kb::scene;
        AnimationClip clip;
        clip.durationSeconds = 1.0F;
        clip.looping = true;
        clip.tracks.push_back({.targetPath=".", .keyframes={{0.0F, {}}, {1.0F, {.position={3,1,0}}}}});
        clip.events={{0.2F,11U},{0.7F,12U}};
        kb::tests::Require(AnimationAssetIO::SaveClip(root/"Assets/Motion.kbanim",clip),"Animation state clip save failed");
        clip.tracks.front().keyframes.back().transform.position={-2,4,0};
        kb::tests::Require(AnimationAssetIO::SaveClip(root/"Assets/Alternate.kbanim",clip),"Alternate state clip save failed");
        AnimatorController definition;
        definition.parameters.push_back({.name="Gain",.type=AnimatorParameterType::Float,.floatDefault=0.5F});
        definition.layers.push_back({.name="Base",.defaultState="First",.states={
            {.id=1U,.name="First",.clipReference="/Game/Motion.kbanim"},
            {.id=2U,.name="Second",.clipReference="/Game/Alternate.kbanim"}}});
        kb::tests::Require(AnimationAssetIO::SaveController(root/"Assets/Controller.kbanimcontroller",definition),
            "Animation state controller save failed");
        TimelineAsset sequence;
        sequence.durationSeconds=2.0F;
        sequence.bindings.push_back({"Actor","."});
        sequence.transformTracks.push_back({"Actor",{{0.0F,{}},{2.0F,{.position={0,8,0}}}}});
        sequence.markers={{0.2F,21U},{1.0F,22U}};
        kb::tests::Require(TimelineAssetIO::Save(root/"Assets/Sequence.kbtimeline",sequence),"Timeline state asset save failed");
    }

    explicit SceneAnimationStateFixture(kb::scene::Scene& value) : scene(value) {
        const auto* definition=scene.Assets().Manager().Registry().FindByPath("/Game/Controller.kbanimcontroller");
        const auto* sequence=scene.Assets().Manager().Registry().FindByPath("/Game/Sequence.kbtimeline");
        kb::tests::Require(definition && sequence,"Animation state fixture metadata missing");
        controller=definition->id;
        animated=scene.Entities().CreateObject({.name="Animated"});
        sequenced=scene.Entities().CreateObject({.name="Sequenced"});
        scene.Components().Animators().Set(animated.Entity(),{
            .controllerAssetId=controller.value,.poseUpdateRateHz=9.0F});
        kb::tests::Require(scene.Components().Animators().Has(animated.Entity()),"Animation state attachment failed");
        timeline=scene.Timelines().Create(sequence->id.value,sequenced.Entity());
        kb::tests::Require(timeline && scene.Timelines().SetPlayback(timeline,0.8F,true) && scene.Timelines().Play(timeline),
            "Animation state timeline creation failed");
    }

    void BeginTransition() {
        kb::tests::Require(scene.Animators().SetFloat(animated.Entity(),"Gain",1.25F)
            && scene.Animators().SetSpeed(animated.Entity(),0.85F)
            && scene.Animators().CrossFade(animated.Entity(),"Base","Second",0.4F,0.1F),
            "Animation state transition setup failed");
    }

    struct Sample {
        std::array<double,12> values;
        std::string state;
        std::string previous;
        bool operator==(const Sample&) const = default;
    };

    Sample Read() const {
        const auto state=scene.Animators().State(animated.Entity(),"Base");
        const auto parameters=scene.Animators().Parameters(animated.Entity());
        kb::tests::Require(state && parameters.size()==1U && scene.Timelines().Exists(timeline),"Animation state vanished");
        const auto pose=scene.Transforms().Get(animated);
        const auto sequencePose=scene.Transforms().Get(sequenced);
        return {{pose.localPosition.x,pose.localPosition.y,state->normalizedTime,state->transitionProgress,
            state->transitioning?1.0:0.0,scene.Animators().Speed(animated.Entity()),parameters.front().floatValue,
            scene.Timelines().Time(timeline),scene.Timelines().IsPlaying(timeline)?1.0:0.0,
            sequencePose.localPosition.x,sequencePose.localPosition.y,
            static_cast<double>(scene.Animators().RuntimeBindingGeneration(animated.Entity()))},state->state,state->previousState};
    }

    void DestroyFuture() {
        kb::tests::Require(scene.Timelines().Release(timeline),"Future timeline release failed");
        scene.Entities().Destroy(animated);
        scene.Entities().Destroy(sequenced);
    }

    void VerifyChangedAssetRefusal(const kb::core::RuntimeStateSnapshot& snapshot) {
        kb::tests::Require(scene.Assets().Manager().Unload(controller),"Animation asset invalidation failed");
        std::string error;
        const auto frame=scene.Runtime().FrameIndex();
        kb::tests::Require(!scene.Runtime().RestoreRuntimeState(snapshot,error)
            && error.find("asset generation")!=std::string::npos && scene.Runtime().FrameIndex()==frame,
            "Scene accepted changed animation resources or mutated before refusal");
    }
};
