#pragma once
#include "csg.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include <functional>

// ============================================================================
// Level Blockout - CSG-based Level Design Examples
// ============================================================================
//
// This file provides example level designs using the CSG system.
// Use these as templates for creating your own level blockouts.
//
// Pattern:
//   1. Define level using CSG::Box, CSG::Union, CSG::Subtract
//   2. Compile to AABBs: auto boxes = CSG::Compile(levelNode);
//   3. Convert to mesh: MeshBuilder builder; builder.PushCSG(boxes);
//   4. Build final mesh: auto mesh = builder.Build();
//
// ============================================================================

namespace LevelBlockout {

    // ============================================================================
    // Simple Room - A basic room with optional doorways
    // ============================================================================

    inline CSG::NodePtr SimpleRoom(
      glm::vec3 center,
      glm::vec3 size,
      float wallThickness = 0.5f,
      bool openFront = false,
      bool openBack = false,
      bool openLeft = false,
      bool openRight = false
    ) {
        // Create outer shell
        auto outer = CSG::Box(center, size);

        // Create inner volume (hollow out)
        glm::vec3 innerSize = size - glm::vec3(wallThickness * 2);
        innerSize.y = size.y;// Keep full height for ceiling cutout
        auto inner = CSG::Box(center + glm::vec3(0, wallThickness, 0), innerSize);

        auto room = CSG::Subtract(outer, inner);

        // Add doorways
        float doorHeight = 2.5f;
        float doorWidth = 1.5f;
        glm::vec3 doorSize;

        if (openFront) {
            doorSize = glm::vec3(doorWidth, doorHeight, wallThickness * 2);
            auto door = CSG::Box(center + glm::vec3(0, -size.y / 2 + doorHeight / 2, size.z / 2), doorSize);
            room = CSG::Subtract(room, door);
        }

        if (openBack) {
            doorSize = glm::vec3(doorWidth, doorHeight, wallThickness * 2);
            auto door = CSG::Box(center + glm::vec3(0, -size.y / 2 + doorHeight / 2, -size.z / 2), doorSize);
            room = CSG::Subtract(room, door);
        }

        if (openLeft) {
            doorSize = glm::vec3(wallThickness * 2, doorHeight, doorWidth);
            auto door = CSG::Box(center + glm::vec3(-size.x / 2, -size.y / 2 + doorHeight / 2, 0), doorSize);
            room = CSG::Subtract(room, door);
        }

        if (openRight) {
            doorSize = glm::vec3(wallThickness * 2, doorHeight, doorWidth);
            auto door = CSG::Box(center + glm::vec3(size.x / 2, -size.y / 2 + doorHeight / 2, 0), doorSize);
            room = CSG::Subtract(room, door);
        }

        return room;
    }

    // ============================================================================
    // Corridor - A long hallway connecting rooms
    // ============================================================================

    inline CSG::NodePtr
      Corridor(glm::vec3 start, glm::vec3 end, float width = 3.0f, float height = 3.0f, float wallThickness = 0.3f) {
        glm::vec3 direction = end - start;
        glm::vec3 center = (start + end) * 0.5f;
        float length = glm::length(direction);

        // Determine orientation (simplified: axis-aligned only)
        glm::vec3 size;
        if (std::abs(direction.x) > std::abs(direction.z)) {
            // X-aligned corridor
            size = glm::vec3(length, height, width);
        } else {
            // Z-aligned corridor
            size = glm::vec3(width, height, length);
        }

        // Create outer shell
        auto outer = CSG::Box(center, size);

        // Create inner volume
        glm::vec3 innerSize = size - glm::vec3(wallThickness * 2, 0, wallThickness * 2);
        innerSize.y = height;// Open at ends
        auto inner = CSG::Box(center + glm::vec3(0, wallThickness, 0), innerSize);

        return CSG::Subtract(outer, inner);
    }

    // ============================================================================
    // Platform - A raised platform with optional stairs
    // ============================================================================

    inline CSG::NodePtr Platform(glm::vec3 center, glm::vec3 size, int stairSteps = 0, float stairWidth = 2.0f) {
        auto platform = CSG::Box(center, size);

        if (stairSteps > 0) {
            float stepHeight = size.y / stairSteps;
            float stepDepth = stairWidth / stairSteps;

            for (int i = 0; i < stairSteps; i++) {
                glm::vec3 stepCenter(
                  center.x,
                  center.y - size.y / 2 + stepHeight * (i + 0.5f),
                  center.z + size.z / 2 + stepDepth * (i + 0.5f)
                );
                glm::vec3 stepSize(size.x, stepHeight, stepDepth);
                auto step = CSG::Box(stepCenter, stepSize);
                platform = CSG::Union(platform, step);
            }
        }

        return platform;
    }

    // ============================================================================
    // Window Cutout - Subtract windows from walls
    // ============================================================================

    inline CSG::NodePtr WindowCutout(
      glm::vec3 wallCenter,
      glm::vec3 wallNormal,// Which way the wall faces
      float windowWidth = 1.5f,
      float windowHeight = 1.5f,
      float sillHeight = 1.0f,
      float wallThickness = 0.5f
    ) {
        glm::vec3 windowSize;
        if (std::abs(wallNormal.x) > 0.5f) {
            // X-facing wall
            windowSize = glm::vec3(wallThickness * 2, windowHeight, windowWidth);
        } else {
            // Z-facing wall
            windowSize = glm::vec3(windowWidth, windowHeight, wallThickness * 2);
        }

        return CSG::Box(wallCenter + glm::vec3(0, sillHeight + windowHeight / 2, 0), windowSize);
    }

    // ============================================================================
    // Pillar - A structural column
    // ============================================================================

    inline CSG::NodePtr Pillar(glm::vec3 base, float radius = 0.5f, float height = 4.0f) {
        // Approximate cylinder with a box (for box-only CSG)
        // Could be enhanced later when cylinder support is added
        return CSG::Box(base + glm::vec3(0, height / 2, 0), glm::vec3(radius * 2, height, radius * 2));
    }

    // ============================================================================
    // Example Levels
    // ============================================================================

    // Simple dungeon room with one doorway
    inline CSG::NodePtr ExampleDungeonRoom() {
        return SimpleRoom(
          glm::vec3(0, 2.5f, 0),// center
          glm::vec3(10, 5, 10),// size
          0.5f,// wall thickness
          true,// open front
          false,
          false,
          false// closed other sides
        );
    }

    // L-shaped corridor
    inline CSG::NodePtr ExampleLCorridor() {
        auto corridor1 = Corridor(glm::vec3(0, 0, 0), glm::vec3(10, 0, 0), 3.0f, 3.0f);

        auto corridor2 = Corridor(glm::vec3(10, 0, 0), glm::vec3(10, 0, 10), 3.0f, 3.0f);

        return CSG::Union(corridor1, corridor2);
    }

    // Room with window
    inline CSG::NodePtr ExampleRoomWithWindow() {
        auto room = SimpleRoom(glm::vec3(0, 2.5f, 0), glm::vec3(8, 5, 8), 0.4f, true, false, false, false);

        // Add window on the right wall
        auto window = WindowCutout(
          glm::vec3(4, 2.5f, 0),// right wall center
          glm::vec3(1, 0, 0),// facing +X
          1.5f,
          1.5f,
          1.0f,
          0.5f
        );

        return CSG::Subtract(room, window);
    }

    // Arena with pillars
    inline CSG::NodePtr ExampleArena() {
        // Main arena floor
        auto floor = CSG::Box(glm::vec3(0, -0.25f, 0), glm::vec3(20, 0.5f, 20));

        // Add corner pillars
        float pillarOffset = 7.0f;
        auto pillar1 = Pillar(glm::vec3(-pillarOffset, 0, -pillarOffset), 0.6f, 5.0f);
        auto pillar2 = Pillar(glm::vec3(pillarOffset, 0, -pillarOffset), 0.6f, 5.0f);
        auto pillar3 = Pillar(glm::vec3(-pillarOffset, 0, pillarOffset), 0.6f, 5.0f);
        auto pillar4 = Pillar(glm::vec3(pillarOffset, 0, pillarOffset), 0.6f, 5.0f);

        auto result = CSG::Union(floor, pillar1);
        result = CSG::Union(result, pillar2);
        result = CSG::Union(result, pillar3);
        result = CSG::Union(result, pillar4);

        return result;
    }

    // Multi-room dungeon layout
    inline CSG::NodePtr ExampleDungeonLayout() {
        // Entry room
        auto entryRoom = SimpleRoom(
          glm::vec3(0, 2.5f, 0), glm::vec3(8, 5, 8), 0.4f, false, true, false, true// open back and right
        );

        // Corridor to main hall
        auto corridor = Corridor(glm::vec3(0, 1.5f, -4), glm::vec3(0, 1.5f, -12), 3.0f, 3.0f);

        // Main hall
        auto mainHall = SimpleRoom(
          glm::vec3(0, 3.0f, -20), glm::vec3(15, 6, 12), 0.5f, true, false, true, true// open front, left, right
        );

        // Side room (connected via right door of entry)
        auto sideRoom = SimpleRoom(
          glm::vec3(10, 2.5f, 0), glm::vec3(6, 5, 6), 0.4f, false, false, true, false// open left
        );

        // Combine all
        auto result = CSG::Union(entryRoom, corridor);
        result = CSG::Union(result, mainHall);
        result = CSG::Union(result, sideRoom);

        // Add a raised platform in main hall
        auto platform = Platform(
          glm::vec3(0, 0.5f, -24),
          glm::vec3(8, 1, 4),
          3,// 3 steps
          2.0f// stair width
        );
        result = CSG::Union(result, platform);

        return result;
    }

    // ============================================================================
    // Utility: Build mesh from CSG
    // ============================================================================

    inline std::shared_ptr<Mesh> BuildLevelMesh(const CSG::NodePtr& level) {
        auto boxes = CSG::Compile(level);

        MeshBuilder builder;
        builder.PushCSG(boxes);
        return builder.Build();
    }

}// namespace LevelBlockout
