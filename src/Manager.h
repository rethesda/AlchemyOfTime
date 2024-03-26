#pragma once

#include "Settings.h"
#include "Utils.h"

class Manager : public Utilities::SaveLoadData {
    
    RE::TESObjectREFR* player_ref = RE::PlayerCharacter::GetSingleton()->As<RE::TESObjectREFR>();
    RE::EffectSetting* empty_mgeff = nullptr;

    std::map<RefID,std::set<FormID>> external_favs;
    std::map<FormFormID,std::pair<int,Count>> handle_crafting_instances; // real-stage:added-total before adding (both real)
    std::map<FormID, bool> is_faved;
    
    bool worldobjectsspoil;

    // Use Or Take Compatibility
    bool po3_use_or_take = false;

    // 0x0003eb42 damage health

    bool listen_activate = true;
    bool listen_crosshair = true;
    bool listen_container_change = true;
    bool listen_menuopenclose = true;

    bool isUninstalled = false;

    std::mutex mutex;
    
    std::vector<Source> sources;

    std::unordered_map<std::string, bool> _other_settings;




#define ENABLE_IF_NOT_UNINSTALLED if (isUninstalled) return;

    [[nodiscard]] Source* GetSource(const FormID real_formid) {
        for (auto& src : sources) {
            if (src.formid == real_formid && !src.init_failed) {
                return &src;
            }
        }
        logger::info("Source not found");
        return nullptr;
    };

    [[nodiscard]] Source* GetStageSource(const FormID stage_formid) {
        if (!stage_formid) return nullptr;
        for (auto& src : sources) {
            for (auto& stage : src.stages) {
                if (stage.second.formid == stage_formid && !src.init_failed) {
                    return &src;
                }
            }
        }
        logger::warn("Stage source not found");
        return nullptr;
    };

    [[nodiscard]] const StageNo GetStageNoFromSource(Source* src,const FormID stage_id) {
        StageNo stage_no=0; // doesnt matter
        if (!src) {
            RaiseMngrErr("Source is null.");
            return stage_no;
        }
        if (auto p_stage_no = src->GetStageNo(stage_id)) stage_no = *p_stage_no;
        else RaiseMngrErr("Stage not found.");
        return stage_no;
	}

    [[nodiscard]] const StageInstance* GetWOStageInstance(RefID wo_refid) {
        if (!wo_refid) {
            RaiseMngrErr("Ref is null.");
			return nullptr;
        }
        for (const auto& src : sources) {
            for (const auto& st_inst : src.data) {
                if (st_inst.location == wo_refid) return &st_inst;
            }
        }
        RaiseMngrErr("Stage instance not found.");
        return nullptr;
    }

    [[nodiscard]] const StageInstance* GetWOStageInstance(RE::TESObjectREFR* wo_ref) {
        if (!wo_ref) {
            RaiseMngrErr("Ref is null.");
            return nullptr;
        }
        return GetWOStageInstance(wo_ref->GetFormID());
	}

    [[nodiscard]] Source* GetWOSource(RefID wo_refid) {
        if (!wo_refid) return nullptr;
        for (auto& src : sources) {
            for (const auto& st_inst : src.data) {
                if (st_inst.location == wo_refid) return &src;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Source* GetWOSource(RE::TESObjectREFR* wo_ref) {
        if (!wo_ref) {
            RaiseMngrErr("Ref is null.");
            return nullptr;
        }
        return GetWOSource(wo_ref->GetFormID());
    }

    void ApplySpoilageInInventory(RE::TESObjectREFR* inventory_owner, Count update_count, FormID old_item, FormID new_item){
        if (new_item == old_item) {
            logger::trace("ApplySpoilageInInventory: New item is the same as the old item.");
            return;
        }
        if (!inventory_owner) return RaiseMngrErr("Inventory owner is null.");
        logger::trace("Updating spoilage in inventory of {} Count {} , Old item {} , New item {}",
                      inventory_owner->GetName(),
                      update_count, old_item, new_item);

        auto inventory = inventory_owner->GetInventory();
        auto entry = inventory.find(RE::TESForm::LookupByID<RE::TESBoundObject>(old_item));
        /*if (entry != inventory.end() && entry->second.second->extraLists && entry->second.second->extraLists->front()) {
			AddItem(inventory_owner, nullptr, new_item, update_count, entry->second.second->extraLists->front());
		} 
        else AddItem(inventory_owner, nullptr, new_item, update_count);*/
        if (entry == inventory.end()) logger::error("Item not found in inventory.");
        else {
            RemoveItemReverse(inventory_owner, nullptr, old_item, std::min(update_count, entry->second.first),
                              RE::ITEM_REMOVE_REASON::kRemove);
        }
        AddItem(inventory_owner, nullptr, new_item, update_count);
        logger::trace("Spoilage updated in inventory.");

    }

    void _UpdateSpoilageInWorld_Fake(RE::TESObjectREFR* wo_ref, RE::ExtraTextDisplayData* xText) {
        if (!xText) {
            logger::error("ExtraTextDisplayData is null.");
            return;
        }
        logger::trace("Setting text display data.");
        wo_ref->extraList.RemoveByType(RE::ExtraDataType::kTextDisplayData);
        wo_ref->extraList.Add(xText);
    }

    void _UpdateSpoilageInWorld_Custom(RE::TESObjectREFR* wo_ref, RE::TESBoundObject* stage_bound) {
        wo_ref->extraList.RemoveByType(RE::ExtraDataType::kTextDisplayData);
        logger::trace("Setting ObjectReference to custom stage form.");
        Utilities::FunctionsSkyrim::SwapObjects(wo_ref, stage_bound);
    }

    void ApplySpoilageInWorld(RE::TESObjectREFR* wo_ref, Stage& stage, bool is_fake){
        if (is_fake) _UpdateSpoilageInWorld_Fake(wo_ref, stage.GetExtraText());
		else _UpdateSpoilageInWorld_Custom(wo_ref, stage.GetBound());
    };

    // updates and makes them take effect in the world
    void UpdateSpoilageInWorld(RE::TESObjectREFR* wo_ref) {
        logger::trace("Updating spoilage in world.");
        if (!wo_ref) return RaiseMngrErr("Ref is null.");
        
        // registered olmak zorunda
        if (!RefIsRegistered(wo_ref->GetFormID())) return RaiseMngrErr("UpdateSpoilageInWorld:Ref is not registered.");
        // get the registered stage instance
        Source* source = GetWOSource(wo_ref);
        if (!source) return RaiseMngrErr("UpdateSpoilageInWorld 2: Source not found.");
        _UpdateSpoilage({wo_ref}, source);
	}

    void AlignRegistries(std::vector<RefID> locs) {
        ENABLE_IF_NOT_UNINSTALLED
        logger::trace("Aligning registries.");
		for (auto& src : sources) {
			for (auto& st_inst : src.data) {
                if (!Utilities::Functions::VectorHasElement(locs,st_inst.location)) continue;
                // POPULATE THIS
                if (st_inst.location == player_refid) HandleConsume(st_inst.xtra.form_id);
			}
		}
    }

    const RE::ObjectRefHandle RemoveItemReverse(RE::TESObjectREFR* moveFrom, RE::TESObjectREFR* moveTo, FormID item_id, Count count,
                                                RE::ITEM_REMOVE_REASON reason) {
        logger::trace("RemoveItemReverse");

        auto ref_handle = RE::ObjectRefHandle();

        if (!moveFrom && !moveTo) {
            RaiseMngrErr("moveFrom and moveTo are both null!");
            return ref_handle;
        }
        logger::trace("Removing item reverse");

        setListenContainerChange(false);

        auto inventory = moveFrom->GetInventory();
        for (auto item = inventory.rbegin(); item != inventory.rend(); ++item) {
            auto item_obj = item->first;
            if (!item_obj) RaiseMngrErr("Item object is null");
            if (item_obj->GetFormID() == item_id) {
                auto inv_data = item->second.second.get();
                if (!inv_data) RaiseMngrErr("Item data is null");
                auto asd = inv_data->extraLists;
                if (!asd || asd->empty()) {
                    logger::trace("Removing item reverse without extra data.");
                    ref_handle = moveFrom->RemoveItem(item_obj, count, reason, nullptr, moveTo);
                } else {
                    /*logger::trace("Removing item reverse with extra data.");
                    for (auto& extra : *asd) {
						if (extra->HasType(RE::ExtraDataType::kOwnership)) {
                            logger::trace("Removing item reverse with ownership.");
						} else {
							logger::trace("Removing item reverse without ownership.");
						}
					}*/
                    ref_handle = moveFrom->RemoveItem(item_obj, count, reason, asd->front(), moveTo);
                }
                setListenContainerChange(true);
                return ref_handle;
            }
        }
        setListenContainerChange(true);
        return ref_handle;
    }

    void AddItem(RE::TESObjectREFR* addTo, RE::TESObjectREFR* addFrom, FormID item_id,
                                                Count count, RE::ExtraDataList* xList=nullptr) {
        logger::trace("AddItem");
        //xList = nullptr;
        if (!addTo && !addFrom) return RaiseMngrErr("moveFrom and moveTo are both null!");
        
        logger::trace("Adding item.");

        setListenContainerChange(false);
        auto bound = RE::TESForm::LookupByID<RE::TESBoundObject>(item_id);
        addTo->AddObjectToContainer(bound, xList, count, addFrom);
        setListenContainerChange(true);
    }

    // registers a new instance to an existing source or creates a new source and registers the instance
    // also tries to handle the case where the item is already a stage item
    void Register(const FormID source_formid, const Count count, const RefID location_refid) {
        if (!source_formid) {
            logger::warn("Formid is null.");
			return;
        }
        if (!count) {
			logger::warn("Count is 0.");
			return;
		}
        if (IsStage(source_formid)) {
            logger::warn("Stage item is being registered!");
            const auto stage_source = GetStageSource(source_formid);
            if (!stage_source) return RaiseMngrErr("Register: Stage source not found.");
            const auto stage_no = GetStageNoFromSource(stage_source, source_formid);
            if (stage_source->IsFakeStage(stage_no)) logger::warn("Register: Fake stage item!");
            // create new instance and register it
            StageInstance new_instance(RE::Calendar::GetSingleton()->GetHoursPassed(), stage_no, count, location_refid);
            if (!stage_source->InsertNewInstance(new_instance)) {
                return RaiseMngrErr("HandlePickUp: InsertNewInstance failed.");
            }
            return;
        }
        // create stages for this instance
        logger::trace("Registering new instance.");
        if (!location_refid) return RaiseMngrErr("Location refid is null.");
        const float curr_time = RE::Calendar::GetSingleton()->GetHoursPassed();
        // make new registry
        auto src = GetSource(source_formid);
        if (!src) {
            Source new_source(source_formid, "", empty_mgeff);
            if (new_source.init_failed) return RaiseMngrErr("Register: New source init failed.");
            StageInstance new_instance(curr_time, 0, count, location_refid);
            if (!new_source.InsertNewInstance(new_instance)) return RaiseMngrErr("Register: InsertNewInstance failed 1.");
            sources.push_back(new_source);
            logger::trace("New source created.");
        } 
        else {
            StageInstance new_instance(curr_time, 0, count, location_refid);
            if (!src->InsertNewInstance(new_instance)) return RaiseMngrErr("Register: InsertNewInstance failed 2.");
        }
    }

    void RaiseMngrErr(const std::string err_msg_ = "Error") {
        logger::critical("{}", err_msg_);
        Utilities::MsgBoxesNotifs::InGame::CustomErrMsg(err_msg_);
        Utilities::MsgBoxesNotifs::InGame::GeneralErr();
        Uninstall();
    }

    void InitFailed() {
        logger::critical("Failed to initialize Manager.");
        Utilities::MsgBoxesNotifs::InGame::InitErr();
        Uninstall();
        return;
    }

    void Init() {

        bool init_failed = false;

        empty_mgeff = RE::IFormFactory::GetConcreteFormFactoryByType<RE::EffectSetting>()->Create();
        empty_mgeff->magicItemDescription = std::string(" ");

        if (init_failed) InitFailed();

        worldobjectsspoil = true;

        po3_use_or_take = Utilities::IsPo3_UoTInstalled();

        // Load also other settings...
        // _other_settings = Settings::LoadOtherSettings();
        logger::info("Manager initialized.");

        // add safety check for the sources size say 5 million
    }

    void Uninstall() {
        isUninstalled = true;
        // Uninstall other settings...
        // Settings::UninstallOtherSettings();
    }

    void setListenActivate(const bool value) {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        listen_activate = value;
    }

    void setListenContainerChange(const bool value) {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        listen_container_change = value;
    }

    void setListenMenuOpenClose(const bool value) {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        listen_menuopenclose = value;
    }

    void setUninstalled(const bool value) {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        isUninstalled = value;
    }

public:
    Manager(std::vector<Source>& data) : sources(data) { Init(); };

    static Manager* GetSingleton(std::vector<Source>& data) {
        static Manager singleton(data);
        return &singleton;
    }

    const char* GetType() override { return "Manager"; }

    void setListenCrosshair(const bool value) {
		std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
		listen_crosshair = value;
	}

    [[nodiscard]] bool getListenCrosshair() {
		std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
		return listen_crosshair;
	}

    [[nodiscard]] bool getListenMenuOpenClose() {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        return listen_menuopenclose;
    }

    [[nodiscard]] bool getListenActivate() {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        return listen_activate;
    }

    [[nodiscard]] bool getListenContainerChange() {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        return listen_container_change;
    }

    [[nodiscard]] bool getPO3UoTInstalled() {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        return po3_use_or_take;
    }

    [[nodiscard]] bool getUninstalled() {
        std::lock_guard<std::mutex> lock(mutex);  // Lock the mutex
        return isUninstalled;
    }

    [[nodiscard]] const bool IsStage(const RE::TESObjectREFR* ref) {
        if (!ref) return false;
        const auto base = ref->GetBaseObject();
        if (!base) return false;
        const auto formid = base->GetFormID();
        return IsStage(formid);
    }

	[[nodiscard]] const bool IsStage(const FormID formid) {
        if (!Settings::IsItem(formid)) {
            logger::trace("Not an item.");
            return false;
        }
		for (const auto& src : sources) {
            if (src.formid == formid) return false;
            for (const auto& stage : src.stages) {
                if (stage.second.formid == formid) return true;
			}
        }
        return false;
    }

    // use it only for world objects! checks if there is a stage instance for the given refid
    [[nodiscard]] const bool RefIsRegistered(const RefID refid) {
        if (!refid) {
            logger::warn("Refid is null.");
            return false;
        }
        for (auto& src : sources) {
            for (auto& st_inst : src.data) {
                //logger::trace("RefIsRegistered: Checking refid {} , st_inst.location {}", refid, st_inst.location);
                if (st_inst.location == refid) return true;
            }
        }
        return false;
    }

    // For stuff generally entering an inventory
    void RegisterAndStartSpoilage(const FormID source_formid, const Count count, const RefID location_refid) {
        Register(source_formid, count, location_refid);
        auto src = GetSource(source_formid);
        if (!src) return RaiseMngrErr("RegisterAndStartSpoilage: Source is null.");
        auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(location_refid);
        if (!ref) return RaiseMngrErr("RegisterAndStartSpoilage1: Ref is null.");
        if (ref->HasContainer() || location_refid == player_refid) {
            logger::trace("Registering in inventory.");
            const auto stage_formid = src->stages[0].formid;
            // to change from the source form to the stage form
            ApplySpoilageInInventory(ref, count, source_formid, stage_formid);
        } else logger::warn("The location is not an inventory. The other overriden function should be used.");
    }

    // For stuff outside
    void RegisterAndStartSpoilage(RE::TESObjectREFR* wo_ref) {
        if (RefIsRegistered(wo_ref->GetFormID())) {
			logger::warn("Ref is already registered.");
			return;
		}
        if (!wo_ref) return RaiseMngrErr("Ref is null.");
        // create stages for this instance
        const auto base = wo_ref->GetBaseObject();
        if (!base) return RaiseMngrErr("Base is null.");
        const FormID formid = base->GetFormID();
        const auto refid = wo_ref->GetFormID();
        const Count count = wo_ref->extraList.GetCount();
        if (!count) {
            logger::warn("Count is 0.");
            return;
        }
        logger::trace("Registering world object. Formid {} , Count {} , Refid {}", formid, count, refid);

        Register(formid, count, wo_ref->GetFormID());
        auto src = GetWOSource(wo_ref);
        if (!src) return RaiseMngrErr("RegisterAndStartSpoilage2: Source is null.");
        //get stage
        Stage& stage = src->stages[0];
        ApplySpoilageInWorld(wo_ref,stage,src->IsFakeStage(0));
    }

    void HandleDrop(const FormID dropped_formid, Count count, RE::TESObjectREFR* dropped_stage_ref){
        ENABLE_IF_NOT_UNINSTALLED
        
        if (!dropped_formid) return RaiseMngrErr("Formid is null.");
        if (!dropped_stage_ref) return RaiseMngrErr("Ref is null.");
        if (!count) {
			logger::warn("Count is 0.");
			return;
		}

        //mutex.lock();

        logger::trace("HandleDrop: dropped_formid {} , Count {}", dropped_formid, count);
        if (!dropped_stage_ref) return RaiseMngrErr("Ref is null.");
        if (!IsStage(dropped_formid)) {
            logger::error("HandleDrop: Not a stage item.");
            return;
        }
        auto source = GetStageSource(dropped_formid);
        if (!source) {
            logger::critical("HandleDrop: Source not found! Fakeform not registered!");
            return;
        }
        
        // count muhabbetleri... ay ay ay
        if (RefIsRegistered(dropped_stage_ref->GetFormID())){
            // eventsink bazen bugliyodu ayni refe gosteriyodu countlar split olunca
            // ama extrayi attiimdan beri olmuyodu
            logger::warn("Ref is registered at HandleDrop!");
            const auto curr_count = dropped_stage_ref->extraList.GetCount();
            Utilities::FunctionsSkyrim::SetObjectCount(dropped_stage_ref, count + curr_count);
            return;
        }
        else if (dropped_stage_ref->extraList.GetCount() != count) {
			logger::warn("HandleDrop: Count mismatch: {} , {}", dropped_stage_ref->extraList.GetCount(), count);
            //dropped_stage_ref->extraList.SetCount(static_cast<uint16_t>(count));
		}
        
        source->PrintData();
        const auto stage_no = GetStageNoFromSource(source, dropped_formid);
        // at the same stage but different start times
        std::vector<StageInstance*> instances_candidates;
        for (auto& st_inst : source->data) {
            if (st_inst.location == player_refid && st_inst.no == stage_no)
                instances_candidates.push_back(&st_inst);
		}
        // need to now order the instances_candidates by their elapsed times
        const auto curr_time = RE::Calendar::GetSingleton()->GetHoursPassed();
        std::sort(instances_candidates.begin(), instances_candidates.end(),
                  [curr_time](StageInstance* a, StageInstance* b) {
            return a->GetElapsed(curr_time) > b->GetElapsed(curr_time);  // use up the old stuff first
		});

        logger::trace("HandleDrop: setting count");
        bool handled_first_stack = false;
        std::vector<RE::TESObjectREFR*> refs_to_be_updated;
        for (auto* instance : instances_candidates) {
            if (!count) break;
            if (count <= instance->count) {
                logger::trace("instance count: {}", instance->count);
                logger::trace("count: {}", count);
                instance->count -= count;
                logger::trace("instance count: {}", instance->count);
                logger::trace("count: {}", count);
                if (!handled_first_stack) {
                    logger::trace("SADSJFH�ADF 1");
                    if (Utilities::FunctionsSkyrim::GetObjectCount(dropped_stage_ref) != static_cast<int16_t>(count)) {
                        Utilities::FunctionsSkyrim::SetObjectCount(dropped_stage_ref, count);
                    }
                    //dropped_stage_ref->extraList.SetOwner(RE::TESForm::LookupByID<RE::TESForm>(0x07));
                    StageInstance new_instance(*instance);
                    new_instance.count = count;
                    new_instance.location = dropped_stage_ref->GetFormID();
                    if (!source->InsertNewInstance(new_instance)) {
                        return RaiseMngrErr("HandleDrop: InsertNewInstance failed.");
                    }
                        
                    refs_to_be_updated.push_back(dropped_stage_ref);
                    handled_first_stack = true;
                } 
                else {
                    logger::trace("SADSJFH�ADF 2");
                    const auto bound_to_drop = instance->xtra.is_fake ? source->GetBoundObject() : instance->GetBound();
                    auto new_ref = Utilities::FunctionsSkyrim::DropObjectIntoTheWorld(bound_to_drop, count, nullptr);
                    if (!new_ref) return RaiseMngrErr("HandleDrop: New ref is null.");
                    if (new_ref->extraList.GetCount() != count) {
						logger::warn("HandleDrop: NewRefCount mismatch: {} , {}", new_ref->extraList.GetCount(), count);
					}
                    StageInstance new_instance(*instance);
                    new_instance.count = count;
                    new_instance.location = new_ref->GetFormID();
                    if (!source->InsertNewInstance(new_instance)) {
                        return RaiseMngrErr("HandleDrop: InsertNewInstance failed.");
                    }
                    ApplySpoilageInWorld(new_ref, source->stages[new_instance.no], source->IsFakeStage(new_instance.no));
                    refs_to_be_updated.push_back(new_ref);
                }
                break;
            } 
            else {
                logger::trace("instance count: {}", instance->count);
                logger::trace("count: {}", count);
                count -= instance->count;
                logger::trace("instance count: {}", instance->count);
                logger::trace("count: {}", count);
                if (!handled_first_stack) {
                    logger::trace("SADSJFH�ADF 3");
                    if (Utilities::FunctionsSkyrim::GetObjectCount(dropped_stage_ref) != static_cast<int16_t>(count)) {
                        Utilities::FunctionsSkyrim::SetObjectCount(dropped_stage_ref, instance->count);
                    }
                    //dropped_stage_ref->extraList.SetOwner(RE::TESForm::LookupByID<RE::TESForm>(0x07));
                    instance->location = dropped_stage_ref->GetFormID();
                    refs_to_be_updated.push_back(dropped_stage_ref);
                    handled_first_stack = true;
                } 
                else {
                    logger::trace("SADSJFH�ADF 4");
                    const auto bound_to_drop = instance->xtra.is_fake ? source->GetBoundObject() : instance->GetBound();
                    auto new_ref =
                        Utilities::FunctionsSkyrim::DropObjectIntoTheWorld(bound_to_drop, instance->count, nullptr);
                    if (!new_ref) return RaiseMngrErr("HandleDrop: New ref is null.");
                    if (new_ref->extraList.GetCount() != instance->count) {
                        logger::warn("HandleDrop: NewRefCount mismatch: {} , {}", new_ref->extraList.GetCount(), instance->count);
                    }
                    instance->location = new_ref->GetFormID();
                    if (instance->xtra.is_fake) _UpdateSpoilageInWorld_Fake(new_ref, source->stages[instance->no].GetExtraText());
					else _UpdateSpoilageInWorld_Custom(new_ref, instance->GetBound());
                    refs_to_be_updated.push_back(new_ref);
                }
            }
        }

        // TODO: add delayer stuff
        _UpdateSpoilage(refs_to_be_updated, source);

        source->CleanUpData();
        source->PrintData();
        //mutex.unlock();
    }

    void HandlePickUp(const FormID pickedup_formid, const Count count, const RefID wo_refid, const bool eat,
                      RE::TESObjectREFR* npc_ref = nullptr) {
        ENABLE_IF_NOT_UNINSTALLED
        logger::info("HandlePickUp: Formid {} , Count {} , Refid {}", pickedup_formid, count, wo_refid);
        const RefID npc_refid = npc_ref ? npc_ref->GetFormID() : player_refid;
        npc_ref = npc_ref ? npc_ref : player_ref; // naming...for the sake of functionality
        if (!RefIsRegistered(wo_refid)) {
            if (worldobjectsspoil){
                // bcs it shoulda been registered already before picking up (with some exceptions)
                logger::warn("HandlePickUp: Not registered world object refid: {}, pickedup_formid: {}", wo_refid,
                             pickedup_formid);
            }
            if (IsStage(pickedup_formid)) {
                logger::warn("HandlePickUp: Unregistered Stage item is being picked up!");
                return Register(pickedup_formid, count, npc_refid);
            }
            return RegisterAndStartSpoilage(pickedup_formid, count, npc_refid);
        }
        // so it was registered before
        auto source = GetStageSource(pickedup_formid); // registeredsa stage olmak zorunda
        if (!source) return RaiseMngrErr("HandlePickUp: Source not found.");
        source->PrintData();
        for (auto& st_inst: source->data) {
            if (st_inst.location == wo_refid) {
                st_inst.location = npc_refid;
                // if it needs to be replaced by a created form
                if (source->IsFakeStage(st_inst.no)) {
                    // try to replace it with fake form
                    ApplySpoilageInInventory(npc_ref, count, pickedup_formid, st_inst.xtra.form_id);
                    if (eat && npc_refid == player_refid) RE::ActorEquipManager::GetSingleton()->EquipObject(RE::PlayerCharacter::GetSingleton(), 
                        st_inst.GetBound(),nullptr, count);
                }
                break;
			}
		}
		source->CleanUpData();
        source->PrintData();
        
        // TODO: add delayer stuff here and prolly also updating
    }

    void HandleConsume(const FormID stage_formid) {
        ENABLE_IF_NOT_UNINSTALLED
        if (!stage_formid) {
            logger::warn("HandleConsume:Formid is null.");
            return;
        }
        if (!IsStage(stage_formid)) {
            logger::warn("HandleConsume:Not a stage item.");
            return;
        }
        auto source = GetStageSource(stage_formid);
        if (!source) {
            logger::warn("HandleConsume:Source not found.");
            return;
        }

        logger::trace("HandleConsume");

        int total_registered_count = 0;
        std::vector<StageInstance*> instances_candidates;
        for (auto& st_inst : source->data) {
            if (st_inst.xtra.form_id == stage_formid && st_inst.location == player_refid) {
                total_registered_count += st_inst.count;
                instances_candidates.push_back(&st_inst);
            }
        }

        // check if player has the item
        // sometimes player does not have the item but it can still be there with count = 0.
        const auto player_inventory = player_ref->GetInventory();
        const auto entry = player_inventory.find(Utilities::FunctionsSkyrim::GetFormByID<RE::TESBoundObject>(stage_formid));
        const auto player_count = entry != player_inventory.end() ? entry->second.first : 0;
        int diff = total_registered_count - player_count;
        if (diff < 0) {
            logger::warn("HandleConsume: Something could have gone wrong with registration.");
            return;
        }
        if (diff == 0) {
            logger::warn("HandleConsume: Nothing to remove.");
            return;
        }
        
        logger::trace("HandleConsume: Adjusting registered count");

        const auto curr_time = RE::Calendar::GetSingleton()->GetHoursPassed();
        std::sort(instances_candidates.begin(), instances_candidates.end(),
                  [curr_time](StageInstance* a, StageInstance* b) { 
                return a->GetElapsed(curr_time) > b->GetElapsed(curr_time);  // eat the older stuff but at same stage
            });

        for (auto& instance : instances_candidates) {
            if (diff <= instance->count) {
                instance->count -= diff;
                break;
			} else {
                diff -= instance->count;
                instance->count = 0;
			}
		}

        source->CleanUpData();
        logger::trace("HandleConsume: updated.");

    }

     // need to put the real items in player's inventory and the fake items in unownedChestOG
    void HandleCraftingEnter(std::string qform_type) {
        ENABLE_IF_NOT_UNINSTALLED
        logger::trace("HandleCraftingEnter");
        
        logger::trace("Crafting menu opened");
        // trusting that the player will leave the crafting menu at some point and everything will be reverted
        std::map<FormID,int> to_remove;
        const auto player_inventory = player_ref->GetInventory();
        for (auto& src : sources) {
            if (src.qFormType != qform_type) continue;
            src.PrintData();
            // just to align reality and registries:
            logger::info("HandleCraftingEnter: Just to align reality and registries");
            //AlignRegistries({player_refid});
            for (auto& st_inst : src.data) {
                if (!st_inst.xtra.crafting_allowed || st_inst.location != player_refid) continue;
                const auto stage_formid = st_inst.xtra.form_id;
                const FormFormID temp = {src.formid, stage_formid};
                if (!handle_crafting_instances.contains(temp)) {
                    const auto stage_bound = st_inst.GetBound();
                    if (!stage_bound) return RaiseMngrErr("HandleCraftingEnter: Stage bound is null.");
                    const auto src_bound = src.GetBoundObject();
                    const auto it = player_inventory.find(src_bound);
                    const auto count_src = it != player_inventory.end() ? it->second.first : 0;
                    handle_crafting_instances[temp] = {st_inst.count, count_src};
                } 
                else handle_crafting_instances[temp].first += st_inst.count;
                if (!is_faved.contains(stage_formid)) is_faved[stage_formid] = Utilities::FunctionsSkyrim::IsFavorited(stage_formid,player_refid);
                else if (!is_faved[stage_formid]) is_faved[stage_formid] = Utilities::FunctionsSkyrim::IsFavorited(stage_formid,player_refid);
            }
        }
        

        for (auto& [formids, counts] : handle_crafting_instances) {
            RemoveItemReverse(player_ref, nullptr, formids.form_id2, counts.first, RE::ITEM_REMOVE_REASON::kRemove);
            AddItem(player_ref, nullptr, formids.form_id1, counts.first);
            logger::trace("Crafting item updated in inventory.");
        }
        // print handle_crafting_instances
        for (auto& [formids, counts] : handle_crafting_instances) {
			logger::info("HandleCraftingEnter: Formid1: {} , Formid2: {} , Count1: {} , Count2: {}", formids.form_id1, formids.form_id2, counts.first, counts.second);
		}

    }

    void HandleCraftingExit() {
        ENABLE_IF_NOT_UNINSTALLED
        logger::trace("HandleCraftingExit");

        logger::trace("Crafting menu closed");
        for (auto& [formids, counts] : handle_crafting_instances) {
            logger::info("HandleCraftingExit: Formid1: {} , Formid2: {} , Count1: {} , Count2: {}", formids.form_id1,
                         formids.form_id2, counts.first, counts.second);
        }

        // need to figure out how many items were used up in crafting and how many were left

        const auto player_inventory = player_ref->GetInventory();
        for (auto& [formids, counts] : handle_crafting_instances) {
            const auto src_bound = Utilities::FunctionsSkyrim::GetFormByID<RE::TESBoundObject>(formids.form_id1);
            const auto it = player_inventory.find(src_bound);
            const auto inventory_count = it != player_inventory.end() ? it->second.first : 0;
            const auto expected_count = counts.first + counts.second;
            auto diff = expected_count - inventory_count; // crafta kullanilan item sayisi
            const auto to_be_removed_added = inventory_count - counts.second;
            if (to_be_removed_added > 0) {
                RemoveItemReverse(player_ref, nullptr, formids.form_id1, to_be_removed_added,
                                  RE::ITEM_REMOVE_REASON::kRemove);
                AddItem(player_ref, nullptr, formids.form_id2, to_be_removed_added);
                bool __faved = is_faved[formids.form_id2];
                if (__faved) Utilities::FunctionsSkyrim::FavoriteItem(formids.form_id2, player_refid);
			}
            if (diff<=0) continue;
            HandleConsume(formids.form_id2);
        }


        handle_crafting_instances.clear();
        is_faved.clear();

    }

    // to check if external container is registered in the corresponding source
    [[nodiscard]] const bool IsExternalContainer(const FormID stage_id, const RefID refid) {
        if (!refid) return false;
        auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(refid);
        if (!ref) return false;
        if (!ref->HasContainer()) return false;
        const auto src = GetStageSource(stage_id);
        if (!src) return false;
        for (const auto& st_inst : src->data) {
            if (st_inst.location == refid) return true;
        }
        return false;
    }

    [[nodiscard]] const bool IsExternalContainer(const RE::TESObjectREFR* external_ref) {
        if (!external_ref) return false;
        if (!external_ref->HasContainer()) return false;
        const auto external_refid = external_ref->GetFormID();
        for (const auto& src : sources) {
			for (const auto& st_inst : src.data) {
                if (st_inst.location == external_refid) return true;
			}
		}
        return false;
    }

    void LinkExternalContainer(const FormID stage_formid, Count item_count, const RefID externalcontainer) {
        ENABLE_IF_NOT_UNINSTALLED
		if (!stage_formid) {
			logger::error("Fake formid is null.");
			return;
		}
        if (!item_count) {
            logger::error("Item count is 0.");
            return;
        }

        const auto external_ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(externalcontainer);
        if (!external_ref) {
            logger::critical("External ref is null.");
            return;
        }
        if (!external_ref->HasContainer()) {
            logger::error("External container does not have a container.");
            return;
        }

        logger::trace("Linking external container.");
        const auto source = GetStageSource(stage_formid);
        if (!source) return RaiseMngrErr("LinkExternalContainer: Source not found.");
        const auto stage_no = GetStageNoFromSource(source, stage_formid);
        std::vector<StageInstance*> instances_candidates;
        for (auto& st_inst : source->data) {
            if (st_inst.no == stage_no && st_inst.location == player_refid) instances_candidates.push_back(&st_inst);
        }
        // need to now order the instances_candidates by their elapsed time
        const auto curr_time = RE::Calendar::GetSingleton()->GetHoursPassed();
        std::sort(instances_candidates.begin(), instances_candidates.end(),
                  [curr_time](StageInstance* a, StageInstance* b) { 
                return a->GetElapsed(curr_time) > b->GetElapsed(curr_time);  // store the older stuff
            });

        for (auto& instance : instances_candidates) {
            if (!item_count) break;
            if (item_count <= instance->count) {
                instance->count -= item_count;
                StageInstance new_instance(*instance);
                new_instance.count = item_count;
                new_instance.location = externalcontainer;
                if (!source->InsertNewInstance(new_instance)) {
                    return RaiseMngrErr("LinkExternalContainer: InsertNewInstance failed.");
                }
                logger::trace("Linked external container.");
                break;
            } 
            else {
                item_count -= instance->count;
                instance->location = externalcontainer;
            }
        }

        source->CleanUpData();

   //     if (Utilities::FunctionsSkyrim::IsFavorited(stage_formid, externalcontainer)) {
   //         logger::trace("Faved item successfully transferred to external container.");
			////external_favs.push_back(stage_formid);
   //     }


        // Stage new_stage(formid, item_count, externalcontainer);
        // SourceDataKey new_sourcedatakey()
        // src->data[chest_refid] = externalcontainer;

        // // add it to handled_external_conts
        // //handled_external_conts.insert(externalcontainer);

        // // if successfully transferred to the external container, check if the fake container is faved
        // if (src->data[chest_refid] != chest_refid &&
        //     IsFavorited(RE::TESForm::LookupByID<RE::TESBoundObject>(fakecontainer), external_ref)) {
        //     logger::trace("Faved item successfully transferred to external container.");
        //     external_favs.push_back(fakecontainer);
        // }
    }

    void UnLinkExternalContainer(const FormID stage_formid, Count count,const RefID externalcontainer) { 
        ENABLE_IF_NOT_UNINSTALLED

        if (!stage_formid) {
			logger::error("Fake formid is null.");
			return;
		}
        if (!count) {
			logger::error("Item count is 0.");
			return;
		}

        if (!externalcontainer) {
            logger::critical("External container is null.");
            return;
        }
        
        if (!IsExternalContainer(stage_formid, externalcontainer)) {
            logger::error("External container is not registered.");
            return;
        }
        if (!IsStage(stage_formid)) {
			logger::error("Not a stage item.");
			return;
		}
        auto source = GetStageSource(stage_formid);
        if (!source) return RaiseMngrErr("UnLinkExternalContainer: Source not found.");
        const auto stage_no = GetStageNoFromSource(source, stage_formid);

        std::vector<StageInstance*> instances_candidates;
        for (auto& st_inst : source->data) {
            if (st_inst.no == stage_no && st_inst.location == externalcontainer) instances_candidates.push_back(&st_inst);
        }
        // need to now order the instances_candidates by their elapsed time
        const auto curr_time = RE::Calendar::GetSingleton()->GetHoursPassed();
        std::sort(instances_candidates.begin(), instances_candidates.end(),
                  [curr_time](StageInstance* a, StageInstance* b) { 
                return a->GetElapsed(curr_time) < b->GetElapsed(curr_time);  // get the good stuff
            });

        for (auto& instance : instances_candidates) {
            if (!count) break;
            if (count <= instance->count) {
                instance->count -= count;
                StageInstance new_instance(*instance);
                new_instance.count = count;
                new_instance.location = player_refid;
                if (!source->InsertNewInstance(new_instance)) {
                    return RaiseMngrErr("UnLinkExternalContainer: InsertNewInstance failed.");
                }
				break;
			} 
            else {
                count -= instance->count;
                instance->location = player_refid;
			}
        }
        
        source->CleanUpData();
        logger::trace("Unlinked external container.");

        // TODO: add delayer stuff here and prolly also updating

        // I need to be able to determine the stage of the stored item

        // I need to get all instances with the externalcontainer and then find the instance with the matching formid among those
        // std::vector<SourceDataKey*> instances_candidates;
        // for (auto& src : sources) {
        //     for (auto& [st_inst, registered_refid] : src.data) {
        //         if (registered_refid == externalcontainer) {
        //             if (st_inst.GetCurrentStage().formid == formid && count <= st_inst.count) {
        //                 instances_candidates.push_back(&st_inst);
        //             }
        //         }
        //     }
        // }
        // if (instances_candidates.empty()) return RaiseMngrErr("UnLinkExternalContainer: No matching instance found.");

        // auto src = GetSource(formid);
        // if (!src) return RaiseMngrErr("Source not found.");
        // for (auto& [st_inst, registered_refid] : src->data) {
        //     if (registered_refid == externalcontainer) {
        //         registered_refid = new_container;
        //         return;
        //     }
        // }
        // // remove it from handled_external_conts
        // //handled_external_conts.erase(externalcontainer);

        // // remove it from external_favs
        // const auto it = std::find(external_favs.begin(), external_favs.end(), fake_container_formid);
        // if (it != external_favs.end()) external_favs.erase(it);

        logger::trace("Unlinked external container.");
    }

    bool _UpdateSpoilage(std::vector<RE::TESObjectREFR*> refs, Source* src) {

        if (!src) {
            RaiseMngrErr("UpdateSpoilage: Source is null.");
            return false;
        }

        std::map<RefID, RE::TESObjectREFR*> ids_refs;
        std::vector<RefID> refids;
        for (auto& ref : refs) {
            if (!ref) {
                RaiseMngrErr("UpdateSpoilage: ref is null.");
                return false;
            }
            ids_refs[ref->GetFormID()] = ref;
            refids.push_back(ref->GetFormID());
        }
        auto updated_stages = src->UpdateAllStages(refids);
        if (updated_stages.empty()) {
			logger::trace("No update");
			return false;
		}

        for (auto& update : updated_stages) {
            if (!update.oldstage || !update.newstage || !update.count || !update.location) {
                logger::error("UpdateSpoilage: Update is null.");
                continue;
            }
            auto ref = ids_refs[update.location];
            if (ref->HasContainer() || ref->IsPlayerRef()) {
                ApplySpoilageInInventory(ref, update.count, update.oldstage->formid, update.newstage->formid);
            }
            // WO
            else if (Settings::IsItem(ref) && worldobjectsspoil) {
                logger::trace("UpdateSpoilage: ref out in the world.");
                if (IsStage(ref) && update.new_is_fake){
                    Utilities::FunctionsSkyrim::SwapObjects(ref, src->GetBoundObject());
                }
                ApplySpoilageInWorld(ref, *update.newstage, update.new_is_fake);
            } 
            else {
                logger::critical("UpdateSpoilage: Unknown ref type.");
                return false;
            }
        }
        src->CleanUpData();
        return true;
    }

    bool UpdateSpoilage(RefID loc_refid) {
        logger::trace("Manager: Updating spoilage for loc_refid {}.",loc_refid);
        return UpdateSpoilage(RE::TESForm::LookupByID<RE::TESObjectREFR>(loc_refid));
    }

    bool UpdateSpoilage(RE::TESObjectREFR* ref) {
        logger::trace("Manager: Updating spoilage.");
        if (!ref) {
            RaiseMngrErr("UpdateSpoilage: ref is null.");
            return false;
        }

        bool update_took_place = false;
        for (auto& src : sources) {
            auto* src_ptr = &src;
            if (!update_took_place) update_took_place = _UpdateSpoilage({ref}, src_ptr);
        }

        if (!update_took_place) {
            logger::trace("No update");
            return false;
        }
        return true;
    }

    // only necessary for the world objects who has fake counterparts
    void SwapWithStage(RE::TESObjectREFR* wo_ref) {
        if (!wo_ref) return RaiseMngrErr("Ref is null.");
        if (!Settings::IsItem(wo_ref)) return;
        if (IsStage(wo_ref)) return;
        
        logger::trace("SwapWithStage");
        
        // 1. registered olmadii icin stage olmayabilir
        // 2. counterparti fake olduu icin disarda base basele represente olabilir
        const auto wo_refid = wo_ref->GetFormID();
        const auto formid = wo_ref->GetBaseObject()->GetFormID();
        if (!RefIsRegistered(wo_ref->GetFormID())) {
            if (worldobjectsspoil) {
                // bcs it shoulda been registered already before picking up (with some exceptions)
                logger::warn("SwapWithStage: Not registered world object refid: {}, formid: {}", wo_refid, formid);
                Print();
            }
            Register(formid, wo_ref->extraList.GetCount(), wo_ref->GetFormID());
            auto source = GetSource(formid);
            if (!source) return RaiseMngrErr("Source not found.");
            Utilities::FunctionsSkyrim::SwapObjects(wo_ref, source->stages[0].GetBound());
            // remove the extra data that cause issues when dropping
            for (const auto& i: Settings::xRemove) 
                wo_ref->extraList.RemoveByType(static_cast<RE::ExtraDataType>(i));
            Utilities::FunctionsSkyrim::PrintObjectExtraData(wo_ref);
            return;
		}
        const auto st_inst = GetWOStageInstance(wo_ref);
        if (!st_inst) {
            logger::warn("SwapWithStage: Stage instance not found.");
			return;
        }
        if (!st_inst->xtra.is_fake) {
            logger::warn("SwapWithStage: Not a fake stage.");
            return;
        }
        // it is fake stage
        Utilities::FunctionsSkyrim::SwapObjects(wo_ref, st_inst->GetBound(), false);
        // remove the extra data that cause issues when dropping
        for (const auto& i : Settings::xRemove)
            wo_ref->extraList.RemoveByType(static_cast<RE::ExtraDataType>(i));
        Utilities::FunctionsSkyrim::PrintObjectExtraData(wo_ref);
        logger::trace("SwapWithStage: Swapped.");
    }

    void Reset() {
		ENABLE_IF_NOT_UNINSTALLED
        logger::info("Resetting manager...");
        for (auto& src : sources) src.Reset();
        sources.clear();
        external_favs.clear();         // we will update this in ReceiveData
        Clear();
        setListenMenuOpenClose(true);
        setListenActivate(true);
        setListenContainerChange(true);
        setListenCrosshair(true);
        setUninstalled(false);
        logger::info("Manager reset.");
	}

    void SendData() {
        ENABLE_IF_NOT_UNINSTALLED
        // std::lock_guard<std::mutex> lock(mutex);
        logger::info("--------Sending data---------");
        Print();
        Clear();
        for (auto& src : sources) {
            Utilities::Types::SaveDataLHS lhs{src.formid, src.editorid};
            Utilities::Types::SaveDataRHS rhs = src.data;
            for (auto& st_inst : rhs) {
                st_inst.xtra.is_favorited = st_inst.location == player_refid && 
                    Utilities::FunctionsSkyrim::IsPlayerFavorited(st_inst.GetBound());
                st_inst.xtra.is_favorited = external_favs.contains(st_inst.location) && 
                    external_favs[st_inst.location].contains(st_inst.xtra.form_id);
			}
            SetData(lhs, rhs);
        }
        logger::info("Data sent.");
    };

    void ReceiveData() {
        logger::info("--------Receiving data---------");

        // std::lock_guard<std::mutex> lock(mutex);

        if (!empty_mgeff) return RaiseMngrErr("ReceiveData: Empty mgeff not there!");

        setListenContainerChange(false);

        for (const auto& [lhs, rhs] : m_Data) {
            const auto source_form = Utilities::FunctionsSkyrim::GetFormByID(lhs.form_id, lhs.editor_id);
            if (!source_form) {
                logger::critical("ReceiveData: Source form not found. Saved formid: {}, editorid: {}", lhs.form_id, lhs.editor_id);
				return RaiseMngrErr("ReceiveData: Source form not found.");
			}
            if (GetSource(source_form->GetFormID())){
            	logger::critical("ReceiveData: Source already exists. Formid {}", source_form->GetFormID());
				return RaiseMngrErr("ReceiveData: Source already exists.");
            }
            Source src(source_form->GetFormID(), "", empty_mgeff);
            if (src.init_failed) {
				logger::critical("ReceiveData: Source init failed. Formid {}", source_form->GetFormID());
                return RaiseMngrErr("ReceiveData: Source init failed.");
            }
			for (const auto& st_inst : rhs) {
                const auto formid = st_inst.xtra.form_id;
                const auto editorid = st_inst.xtra.editor_id;
                const auto form = Utilities::FunctionsSkyrim::GetFormByID(formid, editorid);
                if (!form) {
                    if (st_inst.xtra.is_fake) {
                        logger::info("ReceiveData: Fake form not found. Formid {} Editorid {}", formid, editorid);
                        logger::info("Replacing with new fake form.");
                    }
                    logger::critical("ReceiveData: Form not found. Formid {} Editorid {}", formid, editorid);
					return RaiseMngrErr("ReceiveData: Form not found.");
				}
                StageInstance copyInst(st_inst);
                if (!src.InsertNewInstance(copyInst)) {
					logger::critical("ReceiveData: InsertNewInstance failed. Formid {} Editorid {}", lhs.form_id, lhs.editor_id);
					Utilities::MsgBoxesNotifs::InGame::CustomErrMsg("Failed to receive one of the spoilage instances from your save. \
                        This is expected if you changed/deleted things in your config. Marking this instance as decayed.");
				}
			}
        
        }
        setListenContainerChange(true);
    }

    void Print() {
        for (auto& src : sources) {
		    src.PrintData();
        }
    }

#undef ENABLE_IF_NOT_UNINSTALLED
};