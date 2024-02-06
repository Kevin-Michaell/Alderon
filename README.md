# Alderon
# Unreal Engine 5 - Gameplay Abilities & Teleportation

## Overview

These C++ files are a part of a larger Unreal Engine 5 project `Path of Titans`, a multiplayer survival game that focuses on implementing gameplay abilities using the Gameplay Ability System and integrating a multiplayer system with a push-model network architecture. Additionally, it introduces teleportation features for characters within Points of Interest (POIs).

## Contents

### 1. Gameplay Ability System Integration

The code in this file extensively utilizes the Unreal Engine Gameplay Ability System to implement various character abilities. It includes:

- Ability classes for character skills and actions.
- Attribute and effect customization for character statistics.
- UI system that reflects the granted ability slots of the character.
- Locally saved GameplayEffects and Attributres.

### 2. Multiplayer - Push-Model Network Architecture

The multiplayer functionality in this file follows a push-model network architecture, allowing seamless communication between server and clients. Key features include:

- Replication of gameplay events and state updates.
- Efficient handling of remote procedure calls (RPCs) for multiplayer interactions.
- Synchronization of player actions and abilities across the network.

### 3. Teleportation within Points of Interest (POIs)

To enhance the player experience, teleportation features have been implemented for characters within designated Points of Interest (POIs). This includes:

- Detection of POIs in the game world via Line tracing.
- Smooth transition and teleportation for the character.
- Identify Safe Teleport location for the player (dinosour) based on the species.
