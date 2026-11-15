#pragma once

#include "BrawlerDataAsset.hpp"
#include "RangedDataAsset.hpp"

struct DataAssets {
	BrawlerDataAsset cherry;
	RangedDataAsset banana;
    RangedDataAsset basket;

	DataAssets() {
		cherry.name = "CherryBrawler";
		cherry.max_health = 100;
		cherry.resource_cost = 10;
		cherry.attack_damage = 25;
		cherry.attack_range = 1.0f;
		cherry.attack_rate = 1.0f;
		cherry.movement_speed = 1.0f;
		cherry.troop_radius = 1.0f;

		banana.name = "BananaRanged";
		banana.max_health = 75;
		banana.resource_cost = 15;
		banana.attack_damage = 15;
		banana.attack_range = 7.5f;
		banana.attack_rate = 1.0f;
		banana.movement_speed = 1.25f;
		banana.troop_radius = 1.0f;
		banana.projectile_speed = 5.0f;
		banana.projectile_radius = 0.5f;

        basket.name = "Basket";
		basket.max_health = 1000;
		basket.resource_cost = 10;
		basket.attack_damage = 25;
		basket.attack_range = 1.0f;
		basket.attack_rate = 1.5f;
		basket.movement_speed = 0.0f;
		basket.troop_radius = 3.0f;
	}
};