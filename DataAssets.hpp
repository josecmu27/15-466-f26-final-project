#pragma once

#include "BrawlerDataAsset.hpp"
#include "RangedDataAsset.hpp"
#include "HealerDataAsset.hpp"
#include "FlyerDataAsset.hpp"

struct DataAssets {
	BrawlerDataAsset cherry;
	RangedDataAsset banana;
    RangedDataAsset basket;
	HealerDataAsset pineapple;
	FlyerDataAsset orange;

	DataAssets() {
		cherry.name = "CherryBrawler";
		cherry.label = "Cherry";
		cherry.description = "Melee fighter. Walks up and strikes enemies right beside it.\nHigh health (200) and heavy hits (25), but almost no reach. Costs 10.";
		cherry.max_health = 200;
		cherry.resource_cost = 10;
		cherry.attack_damage = 25;
		cherry.attack_range = 1.0f;
		cherry.attack_rate = 1.0f;
		cherry.movement_speed = 1.0f;
		cherry.troop_radius = 1.0f;

		banana.name = "BananaRanged";
		banana.label = "Banana";
		banana.description = "Ranged fighter. Stops farther away and throws bananas.\nLow health (75), 15 damage, long reach, and a bit faster. Costs 15.";
		banana.max_health = 75;
		banana.resource_cost = 15;
		banana.attack_damage = 15;
		banana.attack_range = 7.5f;
		banana.attack_rate = 1.0f;
		banana.movement_speed = 1.25f;
		banana.troop_radius = 1.0f;
		banana.projectile_speed = 5.0f;
		banana.can_hit_flying = true;
		banana.projectile_radius = 0.5f;

        basket.name = "Basket";
		basket.label = "Basket";
		basket.description = "Your base. It stays put and only fights at arm's length.\n1000 health. You lose if it is destroyed.";
		basket.max_health = 1000;
		basket.resource_cost = 10;
		basket.attack_damage = 25;
		basket.attack_range = 1.0f;
		basket.attack_rate = 1.5f;
		basket.movement_speed = 0.0f;
		
		pineapple.name = "PineappleHealer";
		pineapple.label = "Pineapple";
		pineapple.description = "Support fighter. Hits a nearby enemy, then heals friends around it.\n125 health. Each hit restores 10 health to nearby allies. Costs 15.";
		pineapple.max_health = 125;
		pineapple.resource_cost = 15;
		pineapple.attack_damage = 15;
		pineapple.attack_range = 1.2f;
		pineapple.attack_rate = 1.2f;
		pineapple.movement_speed = 1.0f;
		pineapple.troop_radius = 1.0f;
		pineapple.heal_amount = 10;
		pineapple.heal_radius = 2.5f;

		orange.name = "OrangeFlyer";
		orange.label = "Orange";
		orange.description = "Flying fighter. Crosses the river and obstacles in a straight line.\n120 health, 20 damage in a small area. Ground melee cannot target it. Costs 20.";
		orange.max_health = 120;
		orange.resource_cost = 20;
		orange.attack_damage = 20;
		orange.attack_range = 2.5f;
		orange.attack_rate = 1.4f;
		orange.movement_speed = 1.3f;
		orange.troop_radius = 1.0f;
		orange.splash_radius = 2.0f;
		orange.is_flying = true;
		orange.can_hit_flying = true;
	}
};