# Mod Distribution Notes

BBC Launcher is source-only. Do not commit third-party mod `.jar` files to this repository.

The local test payload can use folders like:

```text
local-test-files\BundledMods\
```

That folder is ignored by git. Builders should download mods from the original project pages or their own approved modpack tooling.

## Current Local Test Mods

These are the mod families that have been tested locally. This table is only a publishing guide; always re-check the upstream project page before shipping a public package.

## Current Local Test Mod Links

These are the current local test mod families used by the launcher payload. Version-specific jar names may change, so use the upstream project pages for downloads and license checks.

| Mod | Used for | Upstream link | GitHub repo guidance |
| --- | --- | --- | --- |
| Controlify | Fabric/NeoForge controller support in default folders | https://modrinth.com/mod/controlify | Do not commit jars; link upstream or download locally. |
| YetAnotherConfigLib (YACL) | Controlify configuration dependency in default folders | https://modrinth.com/mod/yacl | Do not commit jars; link upstream or download locally. |
| Sodium | Modern Fabric/NeoForge renderer performance | https://modrinth.com/mod/sodium | Do not commit jars without checking redistribution terms. |
| Lithium | Modern Fabric/NeoForge game logic performance | https://modrinth.com/mod/lithium | Do not commit jars; link upstream or download locally. |
| FerriteCore | Memory reduction | https://modrinth.com/mod/ferrite-core | Do not commit jars; link upstream or download locally. |
| ImmediatelyFast | Client/UI rendering performance | https://modrinth.com/mod/immediatelyfast | Do not commit jars; link upstream or download locally. |
| Fabric API | Fabric mod support library | https://modrinth.com/mod/fabric-api | Do not commit jars; link upstream or download locally. |
| Legacy4J | Legacy Console Edition-style UI/gameplay folder | https://modrinth.com/mod/legacy4j | Do not commit jars; link upstream or download locally. |
| Factory API | Legacy4J/library support | https://modrinth.com/mod/factory-api | Do not commit jars; link upstream or download locally. |
| Framework | MrCrayfish/Controllable support library | https://www.curseforge.com/minecraft/mc-mods/framework | Do not commit jars; link upstream or download locally. |
| Controllable | Forge/1.12.2 controller support | https://www.curseforge.com/minecraft/mc-mods/controllable | Do not commit jars; link upstream or download locally. |
| BetterFps | 1.12.2 Forge performance | https://www.curseforge.com/minecraft/mc-mods/betterfps | Do not commit jars; link upstream or download locally. |
| FoamFix | 1.12.2 Forge memory/performance | https://modrinth.com/mod/foamfix | Do not commit jars without checking redistribution terms. |
| Phosphor | 1.12.2 lighting performance | https://modrinth.com/mod/phosphor | Do not commit jars; link upstream or download locally. |

Current Legacy4J folders intentionally do not include Controlify or YACL. Legacy4J provides its own console-style control experience, and keeping the extra controller stack out of that folder avoids duplicate controller/config behavior.

OptiFine and Entity Culling are not part of the current local public payload. Do not upload OptiFine or Entity Culling jars to this repository.

## Safe Release Rule

For GitHub source releases:

- Commit source code, scripts, docs, and small generated source helpers only.
- Do not commit Minecraft client jars, assets, libraries, Java runtimes, Forge/Fabric/NeoForge downloaded libraries, mod jars, signing keys, account sessions, or built `.msix` packages.
- If a public binary release ever includes mods, include each upstream license/notice and only include mods whose license or hosting terms allow redistribution.
