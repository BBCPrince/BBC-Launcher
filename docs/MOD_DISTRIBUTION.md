# Mod Distribution Notes

BBC Launcher is source-only. Do not commit third-party mod `.jar` files to this repository.

The local test payload can use folders like:

```text
local-test-files\BundledMods\
```

That folder is ignored by git. Builders should download mods from the original project pages or their own approved modpack tooling.

## Current Local Test Mods

These are the mod families that have been tested locally. This table is only a publishing guide; always re-check the upstream project page before shipping a public package.

| Mod | Known license/status | GitHub repo guidance |
| --- | --- | --- |
| Controlify | LGPL-3.0-or-later on Modrinth | Do not commit jars; link upstream or download locally. |
| YetAnotherConfigLib (YACL) | LGPL-3.0-or-later on Modrinth | Do not commit jars; link upstream or download locally. |
| Sodium | Polyform Shield on Modrinth | Do not commit jars without checking redistribution terms. |
| Lithium | LGPL-3.0-only on Modrinth | Do not commit jars; link upstream or download locally. |
| FerriteCore | MIT on Modrinth | Redistribution is more permissive, but still keep jars out of git. |
| Entity Culling | tr7zw Protective License; upstream says not to redistribute jars outside approved hosts | Do not commit jars. |
| ImmediatelyFast | LGPL-3.0-or-later on Modrinth | Do not commit jars; link upstream or download locally. |
| Fabric API | Apache-2.0 on Modrinth | Do not commit jars; link upstream or download locally. |
| Framework | LGPL-2.1 on CurseForge/GitHub | Do not commit jars; link upstream or download locally. |
| Legacy4J | MIT on Modrinth | Redistribution is more permissive, but still keep jars out of git. |
| Factory API | MIT on Modrinth | Redistribution is more permissive, but still keep jars out of git. |
| Controllable | MIT on CurseForge | Redistribution is more permissive, but still keep jars out of git. |
| BetterFps | LGPL-2.1 on CurseForge | Do not commit jars; link upstream or download locally. |
| FoamFix | Custom license on Modrinth | Do not commit jars without checking redistribution terms. |
| Phosphor | LGPL-3.0-only on Modrinth | Do not commit jars; link upstream or download locally. |
| OptiFine | Upstream copyright page forbids public redistribution without permission | Never commit or bundle jars unless you have written permission. |

## Safe Release Rule

For GitHub source releases:

- Commit source code, scripts, docs, and small generated source helpers only.
- Do not commit Minecraft client jars, assets, libraries, Java runtimes, Forge/Fabric/NeoForge downloaded libraries, mod jars, signing keys, account sessions, or built `.msix` packages.
- If a public binary release ever includes mods, include each upstream license/notice and only include mods whose license or hosting terms allow redistribution.
