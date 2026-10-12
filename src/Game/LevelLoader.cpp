#include"LevelLoader.h"
#include"Game.h"
#include<fstream>
#include<sol/sol.hpp>
#include"../Components/TransformComponent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/SpriteComponent.h"
#include"../Components/AnimationComponent.h"
#include"../Components/BoxColliderComponent.h"
#include"../Components/KeyboardControlledComponent.h"
#include"../Components/CameraFollowComponent.h"
#include"../Components/ProjectileEmitterComponent.h"
#include"../Components/HealthComponent.h"
#include"../Components/TextLabelComponent.h"
#include"../Components/ScriptComponent.h"

LevelLoader::LevelLoader() {

}

LevelLoader::~LevelLoader() {

}

void LevelLoader::LoadLevel(sol::state& lua, const std::unique_ptr<Registry>& registry, const std::unique_ptr<AssetStore>& assetStore, SDL_Renderer* renderer, int levelNum) {

	sol::load_result script = lua.load_file("./assets/scripts/level" + std::to_string(levelNum) + ".lua");
	if (!script.valid()) {
		sol::error err = script;
		std::string errorMessage = err.what();
		Logger::Err("lua sript load err: " + errorMessage);
		return;
	}


	lua.script_file("./assets/scripts/level" + std::to_string(levelNum) + ".lua");

	sol::table level = lua["Level"];

	// 读取资产
	sol::table assets = level["assets"];
	int i = 0;
	while (true) {
		sol::optional<sol::table>hasasset = assets[i];
		if (hasasset == sol::nullopt) {
			break;
		}
		sol::table asset = assets[i];
		std::string assetType = asset["type"];
		std::string assetId = asset["id"];
		if (assetType == "texture") {
			assetStore->AddTexture(renderer, assetId, asset["file"]);
			Logger::Log("加载新纹理 asset id: " + assetId);
		}
		if (assetType == "font") {
			assetStore->AddFont(assetId, asset["file"], asset["font_size"]);
			Logger::Log("加载新字体 asset id: " + assetId);
		}

		i++;
	}

	sol::table map = level["tilemap"];
	std::string mapFilePath = map["map_file"];
	std::string mapTextureAssetId = map["texture_asset_id"];

	// 加载瓦片地图
	int tileSize = map["tile_size"];
	double tileScale = map["scale"];
	int mapNumCols = map["num_cols"];
	int mapNumRows = map["num_rows"];
	std::fstream mapFile;
	mapFile.open(mapFilePath);

	for (int y = 0; y < mapNumRows; y++) {
		for (int x = 0; x < mapNumCols; x++) {
			char ch;
			mapFile.get(ch);
			int srcRectY = std::atoi(&ch) * tileSize;
			mapFile.get(ch);
			int srcRectX = std::atoi(&ch) * tileSize;
			mapFile.ignore();

			Entity tile = registry->CreateEntity();
			tile.Group("tiles");
			tile.AddComponent<TransformComponent>(glm::vec2(x * (tileScale * tileSize), y * (tileScale * tileSize)), glm::vec2(tileScale, tileScale), 0.0);
			tile.AddComponent<SpriteComponent>(mapTextureAssetId, tileSize, tileSize, 0, false, srcRectX, srcRectY);
		}
	}
	mapFile.close();
	Game::mapWidth = mapNumCols * tileSize * tileScale;
	Game::mapHeight = mapNumRows * tileSize * tileScale;

	sol::table entities = level["entities"];
	i = 0;
	while (true) {
		sol::optional<sol::table>hasEntity = entities[i];
		if (hasEntity == sol::nullopt) {
			break;
		}

		sol::table entity = entities[i];
		Entity newEntity = registry->CreateEntity();

		// Tag
		sol::optional<std::string>tag = entity["tag"];
		if (tag != sol::nullopt) {
			newEntity.Tag(entity["tag"]);
		}

		// Group
		sol::optional<std::string>group = entity["group"];
		if (group != sol::nullopt) {
			newEntity.Group(entity["group"]);
		}

		// Components
		sol::optional<sol::table>hasComponent = entity["components"];
		if (hasComponent != sol::nullopt) {
			// Transform
			sol::optional<sol::table>transform = entity["components"]["transform"];
			if (transform != sol::nullopt) {
				newEntity.AddComponent<TransformComponent>(
					glm::vec2(
						entity["components"]["transform"]["position"]["x"],
						entity["components"]["transform"]["position"]["y"]
					),
					glm::vec2(
						entity["components"]["transform"]["scale"]["x"].get_or(1.0),
						entity["components"]["transform"]["scale"]["y"].get_or(1.0)
					),
					entity["components"]["transform"]["rotation"].get_or(0.0)
				);
			}

			// RigidBody
			sol::optional<sol::table>rigidbody = entity["components"]["rigidbody"];
			if (rigidbody != sol::nullopt) {
                glm::vec2 velocity(0);
                sol::optional<sol::table> velocityConfig = rigidbody.value()["velocity"];
                if (velocityConfig) {
                    velocity.x = velocityConfig.value()["x"].get_or(0.0f);
                    velocity.y = velocityConfig.value()["y"].get_or(0.0f);
                }
                newEntity.AddComponent<RigidBodyComponent>(velocity);
			}

            if (rigidbody != sol::nullopt) {
                sol::table config = rigidbody.value();
                auto& body = newEntity.GetComponent<RigidBodyComponent>();
                const std::string type = config["type"].get_or(std::string("dynamic"));
                body.type = type == "static" ? BodyType::Static : type == "kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
                body.mass = config["mass"].get_or(1.0f);
                body.restitution = config["restitution"].get_or(0.5f);
                body.friction = config["friction"].get_or(0.4f);
                body.gravityScale = config["gravity_scale"].get_or(0.0f);
                body.linearDamping = config["linear_damping"].get_or(0.0f);
            }

			// Sprite
			sol::optional<sol::table> sprite = entity["components"]["sprite"];
			if (sprite != sol::nullopt) {
				newEntity.AddComponent<SpriteComponent>(
					entity["components"]["sprite"]["texture_asset_id"],
					entity["components"]["sprite"]["width"],
					entity["components"]["sprite"]["height"],
					entity["components"]["sprite"]["z_index"].get_or(1),
					entity["components"]["sprite"]["fixed"].get_or(false),
					entity["components"]["sprite"]["src_rect_x"].get_or(0),
					entity["components"]["sprite"]["src_rect_y"].get_or(0)
				);
			}

			// Animation
			sol::optional<sol::table> animation = entity["components"]["animation"];
			if (animation != sol::nullopt) {
				newEntity.AddComponent<AnimationComponent>(
					entity["components"]["animation"]["num_frames"].get_or(1),
					entity["components"]["animation"]["speed_rate"].get_or(1)
				);
			}

			// BoxCollider
			sol::optional<sol::table> collider = entity["components"]["boxcollider"];
			if (collider != sol::nullopt) {
				newEntity.AddComponent<BoxColliderComponent>(
					entity["components"]["boxcollider"]["width"],
					entity["components"]["boxcollider"]["height"],
					glm::vec2(
						entity["components"]["boxcollider"]["offset"]["x"].get_or(0),
						entity["components"]["boxcollider"]["offset"]["y"].get_or(0)
					)
				);
			}

            // 配置了通用碰撞体 collider 时，优先使用它替代旧的 boxcollider 配置。
            sol::optional<sol::table> genericCollider = entity["components"]["collider"];
            if (genericCollider != sol::nullopt) {
                sol::table config = genericCollider.value();
                glm::vec2 offset(0);
                sol::optional<sol::table> offsetConfig = config["offset"];
                if (offsetConfig) {
                    offset.x = offsetConfig.value()["x"].get_or(0.0f);
                    offset.y = offsetConfig.value()["y"].get_or(0.0f);
                }
                const std::string shape = config["shape"].get_or(std::string("box"));
                ColliderComponent value = shape == "circle"
                    ? ColliderComponent::Circle(config["radius"].get_or(0.0f), offset)
                    : ColliderComponent(config["width"].get_or(0.0f), config["height"].get_or(0.0f), offset);
                newEntity.AddComponent<ColliderComponent>(value);
            }
            if (genericCollider != sol::nullopt || collider != sol::nullopt) {
                sol::table config = genericCollider != sol::nullopt ? genericCollider.value() : collider.value();
                auto& value = newEntity.GetComponent<ColliderComponent>();
                value.isTrigger = config["is_trigger"].get_or(false);
                value.category = config["category"].get_or(1u);
                value.mask = config["mask"].get_or(0xffffffffu);
            }

			// Health
			sol::optional<sol::table> health = entity["components"]["health"];
			if (health != sol::nullopt) {
				newEntity.AddComponent<HealthComponent>(
					static_cast<int>(entity["components"]["health"]["health_percentage"].get_or(100))
				);
			}

			// ProjectileEmitter
			sol::optional<sol::table> projectileEmitter = entity["components"]["projectile_emitter"];
			if (projectileEmitter != sol::nullopt) {
				newEntity.AddComponent<ProjectileEmitterComponent>(
					glm::vec2(
						entity["components"]["projectile_emitter"]["projectile_velocity"]["x"],
						entity["components"]["projectile_emitter"]["projectile_velocity"]["y"]
					),
					static_cast<int>(entity["components"]["projectile_emitter"]["repeat_frequency"].get_or(1)) * 1000,
					static_cast<int>(entity["components"]["projectile_emitter"]["projectile_duration"].get_or(10)) * 1000,
					static_cast<int>(entity["components"]["projectile_emitter"]["hit_percentage_damage"].get_or(10)),
					entity["components"]["projectile_emitter"]["friendly"].get_or(false)
				);
			}

			// CameraFollow
			sol::optional<sol::table> cameraFollow = entity["components"]["camera_follow"];
			if (cameraFollow != sol::nullopt) {
				newEntity.AddComponent<CameraFollowComponent>();
			}

			// KeyboardControlled
			sol::optional<sol::table> keyboardControlled = entity["components"]["keyboard_controller"];
			if (keyboardControlled != sol::nullopt) {
				newEntity.AddComponent<KeyboardControlledComponent>(
					glm::vec2(
						entity["components"]["keyboard_controller"]["up_velocity"]["x"],
						entity["components"]["keyboard_controller"]["up_velocity"]["y"]
					),
					glm::vec2(
						entity["components"]["keyboard_controller"]["right_velocity"]["x"],
						entity["components"]["keyboard_controller"]["right_velocity"]["y"]
					),
					glm::vec2(
						entity["components"]["keyboard_controller"]["down_velocity"]["x"],
						entity["components"]["keyboard_controller"]["down_velocity"]["y"]
					),
					glm::vec2(
						entity["components"]["keyboard_controller"]["left_velocity"]["x"],
						entity["components"]["keyboard_controller"]["left_velocity"]["y"]
					)
				);
			}

			// Script
			sol::optional<sol::table>script = entity["components"]["on_update_script"];
			if (script != sol::nullopt) {
				sol::function func = entity["components"]["on_update_script"][0];
				newEntity.AddComponent<ScriptComponent>(func);
			}
		}

		i++;
	}
}