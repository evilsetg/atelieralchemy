#include "game/Config.h"
#include "game/Database.h"
#include "game/Perks.h"
#include "game/State.h"
#include "ui/Input.h"
#include "ui/Menu.h"
#include "ui/Renderer.h"
#include "ui/Sound.h"

namespace
{
	// Rolls quality/traits for ingredients as soon as the player picks them up, so
	// that quality reflects the Alchemy skill at harvest time.
	class ContainerWatcher final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
	{
	public:
		static ContainerWatcher* Get()
		{
			static ContainerWatcher watcher;
			return &watcher;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!a_event || !player) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto playerID = player->GetFormID();
			if (a_event->newContainer != playerID && a_event->oldContainer != playerID) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (auto* ingredient = RE::TESForm::LookupByID<RE::IngredientItem>(a_event->baseObj)) {
				const auto counts = player->GetInventoryCounts([&](RE::TESBoundObject& a_obj) { return &a_obj == ingredient; });
				const auto it = counts.find(ingredient);
				game::State::Get().Reconcile(ingredient, it != counts.end() ? it->second : 0);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			game::Database::Get().Load();
			game::Perks::DumpAlchemyPerks();
			game::State::Get().InitForms();
			ui::Menu::Register();
			ui::InputHandler::Register();
			ui::Renderer::Install();
			ui::Sound::Initialize();
			if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
				events->AddEventSink<RE::TESContainerChangedEvent>(ContainerWatcher::Get());
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			game::State::Get().ApplyAll();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logger::info("Atelier Alchemy {} loading", SKSE::GetPluginVersion().string());

	game::Config::Get().Load();

	if (auto* serialization = SKSE::GetSerializationInterface()) {
		serialization->SetUniqueID('ATAL');
		serialization->SetSaveCallback(&game::State::OnSave);
		serialization->SetLoadCallback(&game::State::OnLoad);
		serialization->SetRevertCallback(&game::State::OnRevert);
	}

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logger::critical("Could not register the SKSE message listener");
		return false;
	}
	return true;
}
