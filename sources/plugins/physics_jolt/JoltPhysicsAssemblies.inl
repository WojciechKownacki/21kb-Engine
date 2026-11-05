// Included after the provider's canonical engine/Jolt coordinate conversions.
class JoltPhysicsAssemblies final : public kb::scene::IPhysicsAssemblyBackend {
    using Asset=kb::scene::PhysicsAssemblyAsset;
    using Kind=kb::scene::PhysicsAssemblyKind;
    using StateData=kb::scene::PhysicsAssemblyState;
    struct Record {
        std::uint64_t handle=0;
        SceneEntity owner{};
        Asset asset;
        StateData state;
        std::vector<SceneEntity> generated;
        std::vector<kb::scene::SkeletonBoneId> boneIds;
        std::vector<std::size_t> boneIndices;
        Vec3 rootReference{};
        std::uint64_t skeletonSignature=0;
        JPH::BodyID softBody;
        JPH::BodyID chassis;
        JPH::Ref<JPH::VehicleConstraint> vehicle;
        JPH::Ref<JPH::SoftBodySharedSettings> cloth;
        JPH::Ref<JPH::GroupFilterTable> ragdollGroup;
        std::vector<JPH::BodyID> ragdollBodyIds;
        std::vector<kb::scene::SurfaceGeometryVertex> surface;
        std::vector<std::uint32_t> triangles;
        std::optional<kb::scene::MeshRendererComponent> previousMesh;
        bool ownsBody=false;
        float throttle=0,steering=0,brake=0;
    };
public:
    JoltPhysicsAssemblies(kb::scene::Scene& scene,JPH::PhysicsSystem& physics,JPH::ObjectLayer layer,
        std::function<JPH::BodyID(SceneEntity)> lookup,std::function<void()> flush)
        : scene_(scene),physics_(physics),layer_(layer),lookup_(std::move(lookup)),flush_(std::move(flush)) {}
    ~JoltPhysicsAssemblies() override {Clear();}
    bool Empty() const noexcept {return records_.empty();}

    kb::scene::PhysicsAssemblyResult Create(SceneEntity owner,const Asset& asset) override {
        flush_();scene_.Runtime().SynchronizeTransforms();
        if(records_.size()>=64) return {0,"physics assembly budget exceeded"};
        for(const auto& [id,record]:records_) {
            static_cast<void>(id);
            if(record.owner==owner&&(asset.kind!=Kind::Buoyancy||record.asset.kind==Kind::Buoyancy)) return {0,"owner already has this physics authority"};
        }
        const auto* transform=scene_.Transforms().TryGet(owner);
        if(!transform||std::abs(transform->worldScale.x-1)>0.0001F||std::abs(transform->worldScale.y-1)>0.0001F||std::abs(transform->worldScale.z-1)>0.0001F)
            return {0,"physics assembly owner requires a unit world scale; author dimensions in the asset"};
        const auto precise=scene_.Transforms().WorldTranslation(owner,*transform);
        if(std::max({std::abs(precise.x),std::abs(precise.y),std::abs(precise.z)})>1000000.0) return {0,"physics assembly exceeds supported world bounds"};
        Record record;record.handle=next_++;record.owner=owner;record.asset=asset;record.state.kind=asset.kind;
        try {
            switch(asset.kind) {
            case Kind::Ragdoll: CreateRagdoll(record);break;
            case Kind::Cloth: CreateCloth(record);break;
            case Kind::Vehicle: CreateVehicleBody(record);break;
            case Kind::Fracture: CreateFracturableBox(record);break;
            case Kind::Buoyancy:
                if(const auto* body=scene_.Components().Rigidbodies().TryGet(owner);!body||body->bodyType!=RigidbodyBodyType::Dynamic)
                    throw std::runtime_error("buoyancy requires an existing dynamic rigid body");
                record.state.entities.push_back(owner);break;
            }
        } catch(const std::exception& error) {Release(record);return {0,error.what()};}
        const auto handle=record.handle;records_.emplace(handle,std::move(record));return {handle,{}};
    }
    bool Destroy(std::uint64_t handle,std::string& error) override {
        flush_();const auto found=records_.find(handle);if(found==records_.end()) {error="invalid physics assembly handle";return false;}
        Release(found->second);records_.erase(found);error.clear();return true;
    }
    bool State(std::uint64_t handle,StateData& out,std::string& error) override {
        flush_();auto* record=Find(handle,error);if(!record) return false;out=record->state;return true;
    }
    bool Drive(std::uint64_t handle,float throttle,float steering,float brake,std::string& error) override {
        flush_();auto* record=Find(handle,error,Kind::Vehicle);if(!record) return false;
        record->throttle=throttle;record->steering=steering;record->brake=brake;
        if(const auto body=lookup_(record->owner);!body.IsInvalid()) physics_.GetBodyInterface().ActivateBody(body);return true;
    }
    bool SimulateRagdoll(std::uint64_t handle,bool enabled,std::string& error) override {
        flush_();auto* record=Find(handle,error,Kind::Ragdoll);if(!record) return false;
        if(record->state.simulated==enabled) return true;
        for(const auto bone:record->state.entities) if(!scene_.Components().Rigidbodies().Has(bone)) {error="ragdoll body binding was removed";return false;}
        record->state.simulated=enabled;record->state.ready=false;
        static_cast<void>(scene_.Animators().ClearPhysicsPose(record->owner));
        for(const auto bone:record->state.entities) {
            auto body=*scene_.Components().Rigidbodies().TryGet(bone);
            body.bodyType=enabled?RigidbodyBodyType::Dynamic:RigidbodyBodyType::Kinematic;
            body.linearVelocity={};body.angularVelocity={};scene_.Components().Rigidbodies().Set(bone,body);
        }
        return true;
    }
    bool Fracture(std::uint64_t handle,Vec3 impulse,std::string& error) override {
        flush_();auto* record=Find(handle,error,Kind::Fracture);if(!record) return false;
        if(record->state.fractured) {error="box has already fractured";return false;}
        const auto transform=scene_.Transforms().Get(record->owner);
        const auto origin=scene_.Transforms().WorldTranslation(record->owner,transform);
        for(const auto visual:record->generated) if(scene_.Entities().IsAlive(visual)) scene_.Entities().Destroy(visual);
        record->generated.clear();record->state.entities.clear();
        scene_.Components().Rigidbodies().Remove(record->owner);scene_.Components().Colliders().Remove(record->owner);
        const auto& a=record->asset;
        const Vec3 cell{a.size.x/static_cast<float>(a.cellsX),a.size.y/static_cast<float>(a.cellsY),a.size.z/static_cast<float>(a.cellsZ)};
        const auto count=a.cellsX*a.cellsY*a.cellsZ;
        for(std::uint32_t z=0;z<a.cellsZ;++z) for(std::uint32_t y=0;y<a.cellsY;++y) for(std::uint32_t x=0;x<a.cellsX;++x) {
            const Vec3 center{(static_cast<float>(x)+0.5F)*cell.x-a.size.x*0.5F,(static_cast<float>(y)+0.5F)*cell.y-a.size.y*0.5F,(static_cast<float>(z)+0.5F)*cell.z-a.size.z*0.5F};
            const auto offset=kb::math::Rotate(transform.worldRotation,center);
            const auto entity=MakeEntity(*record,"Fragment",{static_cast<float>(origin.x)+offset.x,static_cast<float>(origin.y)+offset.y,static_cast<float>(origin.z)+offset.z},transform.worldRotation,cell);
            kb::scene::MeshRendererComponent mesh;mesh.meshAssetId=kb::assets::BuiltInShapeId(kb::assets::BuiltInShape::Cube).value;mesh.transient=true;scene_.Components().MeshRenderers().Set(entity,mesh);
            ColliderComponent collider;collider.boxSize={0.99F,0.99F,0.99F};scene_.Components().Colliders().Set(entity,collider);
            RigidbodyComponent body;body.mass=a.mass/static_cast<float>(count);
            body.linearVelocity=impulse*(1.0F/a.mass)+offset*1.5F;scene_.Components().Rigidbodies().Set(entity,body);record->state.entities.push_back(entity);
        }
        record->state.fractured=true;record->state.ready=false;return true;
    }
    bool SetWaterLevel(std::uint64_t handle,float level,std::string& error) override {
        flush_();auto* record=Find(handle,error,Kind::Buoyancy);if(!record) return false;record->asset.waterLevel=level;
        if(const auto body=lookup_(record->owner);!body.IsInvalid()) physics_.GetBodyInterface().ActivateBody(body);return true;
    }

    void PrepareBodies() {
        if(records_.empty()) return;
        bool moved=false;
        for(auto it=records_.begin();it!=records_.end();) {
            if(!scene_.Entities().IsAlive(it->second.owner)) {Release(it->second);it=records_.erase(it);continue;}
            auto& record=it->second;
            if(record.asset.kind==Kind::Ragdoll&&!record.state.simulated) {
                moved=true;
                const auto pose=scene_.Animators().InstanceSkeleton(record.owner);
                if(!pose||pose->compatibilitySignature!=record.skeletonSignature) throw std::runtime_error("ragdoll skeleton changed or is unavailable");
                const auto owner=scene_.Transforms().Get(record.owner);
                const auto origin=scene_.Transforms().WorldTranslation(record.owner,owner);
                for(std::size_t i=0;i<record.state.entities.size();++i) {
                    const auto index=record.boneIndices[i];
                    const auto rotation=kb::math::Normalize(owner.worldRotation*pose->currentComponentPose.rotations[index]);
                    const auto offset=kb::math::Rotate(owner.worldRotation,pose->currentComponentPose.positions[index])+kb::math::Rotate(rotation,record.asset.bones[i].center);
                    auto body=scene_.Transforms().Get(record.state.entities[i]);
                    body.localPosition={static_cast<float>(origin.x)+offset.x,static_cast<float>(origin.y)+offset.y,static_cast<float>(origin.z)+offset.z};body.localRotation=rotation;
                    scene_.Transforms().Set(record.state.entities[i],body);
                }
            }
            ++it;
        }
        if(moved) scene_.Runtime().SynchronizeTransforms();
    }
    void BeforeStep(float deltaSeconds) {
        for(auto& [handle,record]:records_) {
            static_cast<void>(handle);
            if(record.asset.kind==Kind::Ragdoll) {
                for(std::size_t i=0;i<record.state.entities.size();++i) {
                    const auto body=lookup_(record.state.entities[i]);
                    if(!body.IsInvalid() && record.ragdollBodyIds[i]!=body) {
                        physics_.GetBodyInterface().SetCollisionGroup(body,JPH::CollisionGroup(record.ragdollGroup,static_cast<JPH::CollisionGroup::GroupID>(record.handle),static_cast<JPH::CollisionGroup::SubGroupID>(i)));
                        record.ragdollBodyIds[i]=body;
                    }
                }
            } else if(record.asset.kind==Kind::Vehicle) {
                const auto body=lookup_(record.owner);
                if(body.IsInvalid()) continue;
                if(!record.vehicle) InitializeVehicle(record,body);
                if(record.chassis!=body) throw std::runtime_error("vehicle chassis changed while its assembly was active");
                auto* controller=static_cast<JPH::WheeledVehicleController*>(record.vehicle->GetController());
                controller->SetDriverInput(record.throttle,record.steering,record.brake,0);
                if(record.throttle!=0||record.steering!=0) physics_.GetBodyInterface().ActivateBody(body);
            } else if(record.asset.kind==Kind::Buoyancy) {
                const auto body=lookup_(record.owner);
                if(body.IsInvalid()) continue;
                const auto& a=record.asset;
                record.state.submerged=physics_.GetBodyInterface().ApplyBuoyancyImpulse(body,JPH::RVec3(0,a.waterLevel,0),JPH::Vec3::sAxisY(),
                    a.buoyancy,a.linearDrag,a.angularDrag,ToJolt(a.fluidVelocity),physics_.GetGravity(),deltaSeconds);
                record.state.ready=true;
            }
        }
    }
    void AfterStep() {
        for(auto& [handle,record]:records_) {
            static_cast<void>(handle);if(!scene_.Entities().IsAlive(record.owner)) continue;
            switch(record.asset.kind) {
            case Kind::Cloth: PublishCloth(record);break;
            case Kind::Ragdoll: PublishRagdoll(record);break;
            case Kind::Vehicle:
                if(record.vehicle) {
                    record.state.ready=true;record.state.wheelContacts=0;
                    for(const auto* wheel:record.vehicle->GetWheels()) if(wheel->HasContact()) ++record.state.wheelContacts;
                    record.state.speed=physics_.GetBodyInterface().GetLinearVelocity(record.chassis).Length();
                    UpdateVehicleVisuals(record);
                }
                break;
            case Kind::Fracture:
                record.state.ready=std::all_of(record.state.entities.begin(),record.state.entities.end(),[&](auto entity) {return !lookup_(entity).IsInvalid();});break;
            case Kind::Buoyancy:
                if(const auto body=lookup_(record.owner);!body.IsInvalid()) record.state.speed=physics_.GetBodyInterface().GetLinearVelocity(body).Length();break;
            }
        }
    }
    void Clear() noexcept {
        for(auto& [handle,record]:records_) {static_cast<void>(handle);Release(record);}records_.clear();
    }
private:
    Record* Find(std::uint64_t handle,std::string& error,std::optional<Kind> kind={}) {
        error.clear();const auto found=records_.find(handle);
        if(found==records_.end()||!scene_.Entities().IsAlive(found->second.owner)) {error="invalid or expired physics assembly handle";return nullptr;}
        if(kind&&found->second.asset.kind!=*kind) {error="operation does not apply to this physics assembly kind";return nullptr;}
        return &found->second;
    }
    SceneEntity MakeEntity(Record& record,std::string_view name,Vec3 position,Quat rotation={},Vec3 scale={1,1,1}) {
        kb::scene::SceneObjectDesc desc;desc.name=std::string{name}+"_"+std::to_string(record.handle)+"_"+std::to_string(record.generated.size());
        desc.transform.localPosition=position;desc.transform.localRotation=rotation;desc.transform.localScale=scale;
        const auto entity=scene_.Entities().CreateEntity(desc);if(!entity.IsValid()) throw std::runtime_error("could not create physics assembly entity");
        record.generated.push_back(entity);return entity;
    }
    SceneEntity MakeVisual(Record& record,kb::assets::BuiltInShape shape,Vec3 position,Quat rotation,Vec3 scale) {
        const auto entity=MakeEntity(record,"PhysicsVisual",position,rotation,scale);
        kb::scene::MeshRendererComponent mesh;mesh.meshAssetId=kb::assets::BuiltInShapeId(shape).value;mesh.transient=true;scene_.Components().MeshRenderers().Set(entity,mesh);return entity;
    }
    void CreateRagdoll(Record& record) {
        const auto pose=scene_.Animators().InstanceSkeleton(record.owner);
        const auto* animator=scene_.Components().Animators().TryGet(record.owner);
        if(!pose||!animator||animator->rootMotionOwner!=kb::scene::AnimatorRootMotionOwner::None) throw std::runtime_error("ragdoll requires an initialized skeletal Animator without root-motion ownership");
        if(scene_.Hierarchy().Parent(record.owner).IsValid()) throw std::runtime_error("ragdoll owner must be a hierarchy root");
        const auto skeleton=scene_.Assets().Manager().Load<kb::scene::SkeletonAsset>(kb::assets::AssetId{pose->skeletonAssetId});
        if(!skeleton.IsLoaded()) throw std::runtime_error("ragdoll skeleton asset cannot be loaded");
        for(const auto& definition:record.asset.bones) {
            const auto found=std::find_if(skeleton->bones.begin(),skeleton->bones.end(),[&](const auto& bone) {return bone.name==definition.name;});
            if(found==skeleton->bones.end()) throw std::runtime_error("ragdoll bone was not found: "+definition.name);
            const auto index=static_cast<std::size_t>(found-skeleton->bones.begin());record.boneIds.push_back(found->id);record.boneIndices.push_back(index);
        }
        record.ragdollGroup=new JPH::GroupFilterTable(static_cast<JPH::uint>(record.asset.bones.size()));
        record.ragdollBodyIds.resize(record.asset.bones.size());
        for(std::size_t i=1;i<record.asset.bones.size();++i) record.ragdollGroup->DisableCollision(static_cast<JPH::uint>(i),static_cast<JPH::uint>(record.asset.bones[i].parent));
        record.skeletonSignature=pose->compatibilitySignature;record.rootReference=pose->currentComponentPose.positions[record.boneIndices[0]];
        const auto owner=scene_.Transforms().Get(record.owner);const auto origin=scene_.Transforms().WorldTranslation(record.owner,owner);
        std::vector<Vec3> boneOrigins;
        for(std::size_t i=0;i<record.asset.bones.size();++i) {
            const auto& definition=record.asset.bones[i];const auto index=record.boneIndices[i];
            const auto rotation=kb::math::Normalize(owner.worldRotation*pose->currentComponentPose.rotations[index]);
            const auto offset=kb::math::Rotate(owner.worldRotation,pose->currentComponentPose.positions[index]);
            const Vec3 boneOrigin{static_cast<float>(origin.x)+offset.x,static_cast<float>(origin.y)+offset.y,static_cast<float>(origin.z)+offset.z};boneOrigins.push_back(boneOrigin);
            const auto entity=MakeEntity(record,"PhysicsBone",boneOrigin+kb::math::Rotate(rotation,definition.center),rotation);
            RigidbodyComponent body;body.mass=definition.mass;body.linearDamping=0.05F;body.angularDamping=0.1F;scene_.Components().Rigidbodies().Set(entity,body);
            ColliderComponent collider;collider.shape=ColliderShape::Capsule;collider.radius=definition.radius;collider.height=definition.height;scene_.Components().Colliders().Set(entity,collider);
            record.state.entities.push_back(entity);
        }
        scene_.Runtime().SynchronizeTransforms();
        for(std::size_t i=1;i<record.asset.bones.size();++i) {
            const auto parent=static_cast<std::size_t>(record.asset.bones[i].parent);
            const auto childTransform=scene_.Transforms().Get(record.state.entities[i]),parentTransform=scene_.Transforms().Get(record.state.entities[parent]);
            JointComponent joint;joint.type=JointType::Point;joint.connectedEntity=record.state.entities[parent];
            joint.anchor=kb::math::Rotate(kb::math::Conjugate(childTransform.worldRotation),boneOrigins[i]-childTransform.worldPosition);
            joint.connectedAnchor=kb::math::Rotate(kb::math::Conjugate(parentTransform.worldRotation),boneOrigins[i]-parentTransform.worldPosition);
            scene_.Components().Joints().Set(record.state.entities[i],joint);
        }
    }
    void CreateCloth(Record& record) {
        const auto surfaces=kb::scene::SceneSurfaceGeometry::All(scene_);
        if(std::any_of(surfaces.begin(),surfaces.end(),[&](const auto& surface){return surface.owner==record.owner;})) throw std::runtime_error("owner already has a dynamic surface");
        record.cloth=new JPH::SoftBodySharedSettings;
        const auto& a=record.asset;const auto count=a.columns*a.rows;record.surface.resize(count);
        for(std::uint32_t y=0;y<a.rows;++y) for(std::uint32_t x=0;x<a.columns;++x) {
            const auto index=y*a.columns+x;const Vec3 position{(static_cast<float>(x)-static_cast<float>(a.columns-1)*0.5F)*a.spacing,-static_cast<float>(y)*a.spacing,0};
            record.cloth->mVertices.emplace_back(JPH::Float3(position.x,position.y,position.z),JPH::Float3(0,0,0),a.pinTopEdge&&y==0?0.0F:static_cast<float>(count)/a.mass);
            record.surface[index]={.position=position,.normal={0,0,-1},.u=static_cast<float>(x)/static_cast<float>(a.columns-1),.v=static_cast<float>(y)/static_cast<float>(a.rows-1)};
        }
        for(std::uint32_t y=0;y+1<a.rows;++y) for(std::uint32_t x=0;x+1<a.columns;++x) {
            const auto first=y*a.columns+x,below=first+a.columns;
            record.triangles.insert(record.triangles.end(),{first,first+1,below,first+1,below+1,below});
            record.cloth->AddFace({first,first+1,below});record.cloth->AddFace({first+1,below+1,below});
        }
        const JPH::SoftBodySharedSettings::VertexAttributes attributes(a.compliance,a.compliance,a.bendingCompliance);
        record.cloth->CreateConstraints(&attributes,1);record.cloth->Optimize();
        const auto transform=scene_.Transforms().Get(record.owner);
        JPH::SoftBodyCreationSettings settings(record.cloth,ToJoltPosition(scene_.Transforms().WorldTranslation(record.owner,transform)),ToJolt(transform.worldRotation),layer_);
        settings.mUpdatePosition=false;settings.mMakeRotationIdentity=false;settings.mNumIterations=8;settings.mVertexRadius=0.02F;settings.mUserData=record.owner.Id();
        auto* body=physics_.GetBodyInterface().CreateSoftBody(settings);if(!body) throw std::runtime_error("soft-body allocation failed");
        record.softBody=body->GetID();physics_.GetBodyInterface().AddBody(record.softBody,JPH::EActivation::Activate);
        if(const auto* mesh=scene_.Components().MeshRenderers().TryGet(record.owner)) record.previousMesh=*mesh;
        std::string error;if(!kb::scene::SceneSurfaceGeometry::Publish(scene_,record.owner,record.surface,record.triangles,error)) throw std::runtime_error(error);
        record.state.entities={record.owner};record.state.ready=true;
    }
    void AddOwnerBody(Record& record,RigidbodyBodyType type) {
        if(scene_.Components().Rigidbodies().Has(record.owner)||scene_.Components().Colliders().Has(record.owner)) throw std::runtime_error("assembly creates its body; use an owner without Rigidbody/Collider");
        ColliderComponent collider;collider.boxSize=record.asset.size;scene_.Components().Colliders().Set(record.owner,collider);
        RigidbodyComponent body;body.bodyType=type;body.mass=record.asset.mass;body.linearDamping=0.01F;scene_.Components().Rigidbodies().Set(record.owner,body);record.ownsBody=true;
    }
    void CreateVehicleBody(Record& record) {
        AddOwnerBody(record,RigidbodyBodyType::Dynamic);
        const auto transform=scene_.Transforms().Get(record.owner);
        MakeVisual(record,kb::assets::BuiltInShape::Cube,transform.worldPosition,transform.worldRotation,record.asset.size);
        record.state.entities={record.owner};
        for(unsigned wheel=0;wheel<4;++wheel) record.state.entities.push_back(MakeVisual(record,kb::assets::BuiltInShape::Cylinder,transform.worldPosition,{},
            {record.asset.wheelRadius*2,0.12F,record.asset.wheelRadius*2}));
    }
    void InitializeVehicle(Record& record,JPH::BodyID chassis) {
        JPH::VehicleConstraintSettings settings;
        const auto& a=record.asset;
        for(unsigned i=0;i<4;++i) {
            auto* wheel=new JPH::WheelSettingsWV;
            wheel->mPosition=JPH::Vec3((i%2==0?-0.5F:0.5F)*a.size.x,-a.size.y*0.5F,(i<2?0.35F:-0.35F)*a.size.z);
            wheel->mRadius=a.wheelRadius;wheel->mWidth=0.2F;wheel->mSuspensionMinLength=0.05F;wheel->mSuspensionMaxLength=a.suspensionLength;
            wheel->mMaxSteerAngle=i<2?0.5F:0.0F;settings.mWheels.push_back(wheel);
        }
        auto* controller=new JPH::WheeledVehicleControllerSettings;controller->mEngine.mMaxTorque=a.engineTorque;
        controller->mDifferentials.resize(1);controller->mDifferentials[0].mLeftWheel=2;controller->mDifferentials[0].mRightWheel=3;settings.mController=controller;
        JPH::BodyLockWrite lock(physics_.GetBodyLockInterface(),chassis);if(!lock.Succeeded()) throw std::runtime_error("vehicle chassis is unavailable");
        record.vehicle=new JPH::VehicleConstraint(lock.GetBody(),settings);record.chassis=chassis;
        record.vehicle->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterRay(layer_));
        physics_.AddConstraint(record.vehicle);physics_.AddStepListener(record.vehicle);
    }
    void CreateFracturableBox(Record& record) {
        AddOwnerBody(record,RigidbodyBodyType::Static);const auto transform=scene_.Transforms().Get(record.owner);
        MakeVisual(record,kb::assets::BuiltInShape::Cube,transform.worldPosition,transform.worldRotation,record.asset.size);
    }
    void PublishCloth(Record& record) {
        JPH::BodyLockRead lock(physics_.GetBodyLockInterface(),record.softBody);if(!lock.Succeeded()) throw std::runtime_error("cloth body was removed unexpectedly");
        const auto* motion=static_cast<const JPH::SoftBodyMotionProperties*>(lock.GetBody().GetMotionProperties());
        const auto& vertices=motion->GetVertices();if(vertices.size()!=record.surface.size()) throw std::runtime_error("cloth topology changed unexpectedly");
        record.state.vertices.clear();record.state.vertices.reserve(vertices.size());
        for(std::size_t i=0;i<vertices.size();++i) {record.surface[i].position=FromJolt(vertices[i].mPosition);record.surface[i].normal={};record.state.vertices.push_back(record.surface[i].position);}
        for(std::size_t i=0;i<record.triangles.size();i+=3) {
            const auto a=record.triangles[i],b=record.triangles[i+1],c=record.triangles[i+2];
            const auto normal=kb::math::Cross(record.surface[b].position-record.surface[a].position,record.surface[c].position-record.surface[a].position);
            for(const auto index:{a,b,c}) record.surface[index].normal=record.surface[index].normal+normal;
        }
        for(auto& vertex:record.surface) vertex.normal=kb::math::Dot(vertex.normal,vertex.normal)>1.0e-12F?kb::math::Normalize(vertex.normal):Vec3{0,0,-1};
        std::string error;if(!kb::scene::SceneSurfaceGeometry::Publish(scene_,record.owner,record.surface,record.triangles,error)) throw std::runtime_error(error);
        record.state.ready=true;
    }
    void PublishRagdoll(Record& record) {
        record.state.ready=std::all_of(record.state.entities.begin(),record.state.entities.end(),[&](auto entity) {return !lookup_(entity).IsInvalid();});
        if(!record.state.ready||!record.state.simulated) return;
        const auto pose=scene_.Animators().InstanceSkeleton(record.owner);
        if(!pose||pose->compatibilitySignature!=record.skeletonSignature) throw std::runtime_error("ragdoll skeleton binding changed");
        std::vector<kb::scene::AnimatorPhysicsBonePose> targets;targets.reserve(record.state.entities.size());
        for(std::size_t i=0;i<record.state.entities.size();++i) {
            const auto body=scene_.Transforms().Get(record.state.entities[i]);
            kb::scene::AnimatorIkTarget target;target.worldRotation=body.worldRotation;
            target.worldPosition=body.worldPosition-kb::math::Rotate(body.worldRotation,record.asset.bones[i].center);
            targets.push_back({record.boneIds[i],target});
        }
        auto owner=scene_.Transforms().Get(record.owner);
        owner.localPosition=targets.front().target.worldPosition-kb::math::Rotate(owner.worldRotation,record.rootReference);
        scene_.Transforms().Set(record.owner,owner);scene_.Runtime().SynchronizeTransforms();
        if(!scene_.Animators().SetPhysicsPose(record.owner,targets)) throw std::runtime_error("ragdoll could not publish its skeleton pose");
    }
    void UpdateVehicleVisuals(Record& record) {
        if(record.generated.size()!=5) throw std::runtime_error("vehicle visual binding was lost");
        auto chassis=scene_.Transforms().Get(record.generated[0]);const auto owner=scene_.Transforms().Get(record.owner);
        chassis.localPosition=owner.worldPosition;chassis.localRotation=owner.worldRotation;scene_.Transforms().Set(record.generated[0],chassis);
        for(unsigned i=0;i<4;++i) {
            const auto world=record.vehicle->GetWheelWorldTransform(i,JPH::Vec3::sAxisY(),JPH::Vec3::sAxisZ());
            auto wheel=scene_.Transforms().Get(record.generated[i+1]);wheel.localPosition=FromJoltPosition(world.GetTranslation());wheel.localRotation=FromJolt(world.GetQuaternion());
            scene_.Transforms().Set(record.generated[i+1],wheel);
        }
    }
    void Release(Record& record) noexcept {
        if(record.vehicle) {physics_.RemoveStepListener(record.vehicle);physics_.RemoveConstraint(record.vehicle);record.vehicle=nullptr;}
        if(!record.softBody.IsInvalid()) {physics_.GetBodyInterface().RemoveBody(record.softBody);physics_.GetBodyInterface().DestroyBody(record.softBody);record.softBody=JPH::BodyID{};}
        if(record.asset.kind==Kind::Ragdoll) static_cast<void>(scene_.Animators().ClearPhysicsPose(record.owner));
        if(record.asset.kind==Kind::Cloth) {
            kb::scene::SceneSurfaceGeometry::Remove(scene_,record.owner);
            if(record.previousMesh&&scene_.Entities().IsAlive(record.owner)) scene_.Components().MeshRenderers().Set(record.owner,*record.previousMesh);
        }
        for(const auto entity:record.generated) if(scene_.Entities().IsAlive(entity)) scene_.Entities().Destroy(entity);
        if(record.ownsBody&&scene_.Entities().IsAlive(record.owner)) {scene_.Components().Rigidbodies().Remove(record.owner);scene_.Components().Colliders().Remove(record.owner);}
    }
    kb::scene::Scene& scene_;
    JPH::PhysicsSystem& physics_;
    JPH::ObjectLayer layer_;
    std::function<JPH::BodyID(SceneEntity)> lookup_;
    std::function<void()> flush_;
    std::uint64_t next_=1;
    std::map<std::uint64_t,Record> records_;
};
