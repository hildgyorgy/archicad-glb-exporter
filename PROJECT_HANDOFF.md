# Drop & View GLB Exporter – fejlesztői átadó

Állapot: **2026-10-27**. Az aktuális publikus kiadás **v1.2.0**. Ez a fájl a gépváltás utáni tájékozódáshoz készült; a kód, a tesztek és a [README](README.md) az elsődleges források.

## Gépváltási pillanatkép

- A dokumentum létrehozása előtt a helyi `main` munkafa tiszta volt és egyezett az `origin/main` ággal. Fejcommit és a `v1.2.0` tag: **`c62f364e4dfc075fc3f16a802daf25b03afe62e5`**. A másik gépen először `git fetch` és `git status -sb` segítségével ellenőrizd a szinkront.
- A [v1.2.0 release](https://github.com/hildgyorgy/archicad-glb-exporter/releases/tag/v1.2.0) publikus. A [Publish Full Release #21](https://github.com/hildgyorgy/archicad-glb-exporter/actions/runs/40402285073) sikeresen építette az Archicad **28, 29 és 30** macOS és Windows add-onjait, aláírta/notarizálta a macOS DMG-t, majd publikálta a négy fájlt: DMG, ZIP és a két SHA-256 ellenőrzőösszeg. A letöltött csomagok hash-e egyezett, és mindkettőben ellenőriztük a három Archicad-verzió alkönyvtárát.
- A v1.2.0 előkészítésekor helyben **5/5 portable teszt** sikeres volt, és az Archicad 30 macOS add-on lefordult. Archicad 28/29 helyi DevKit nem volt elérhető; ezek buildje a GitHub Actionsben sikerült. A párbeszédablak kézi, mindhárom Archicad-verzióban végzett UI-próbájáról nincs visszajelzés ebben a szálban.
- A korábbi gép `build-tests/`, `build-ac30/`, `/tmp` és Desktop fájljai helyiek, nincsenek a repóban. A másik gépen a függőségeket és a megfelelő Graphisoft API DevKitet újra meg kell találni/telepíteni. Tanúsítványt, jelszót vagy egyéb kiadási titkot ne másolj át a repóba; a GitHub Actions a beállított repository secrets értékeket használja.

## A v1.2.0 szerkesztési policy

- Az `ExportOptionsDialog` új **ALLOW MATERIAL EDITING IN DROP & VIEW** jelölőnégyzete minden párbeszédablak megnyitásakor **kikapcsolt**. Az állapot nem preference; a design credits régi preference-e továbbra is működik.
- Az adatút: `Src/GlbExporter.cpp` dialógus → `Glb::Model::allowMaterialEditing` a `Src/GlbWriter.hpp` fájlban → `Src/GlbWriter.cpp`. Minden új GLB `asset.extras.dropView` objektuma tartalmazza a `schemaVersion: 1` és `materialWorkbench: {"policy": "disabled" | "enabled"}` mezőket, akkor is, ha más Drop & View metadata nincs. Az initial view, sun, window lights és design credits metadata megmarad.
- Ez a Drop & View által tiszteletben tartott szerzői szándék, **nem DRM, titkosítás vagy másolásvédelem**. A GLB más programban továbbra is nyitható és módosítható. A viewer külön repó; jelenlegi handoffja szerint a `disabled` fájlt nem engedi a Material Workbenchben szerkeszteni, hiányzó/ismeretlen policy esetén a régi és külső fájlokat engedi.
- A writer-fixture és a Python validátor a két explicit értéket, a metadata nélküli esetet, a régi metadata megőrzését, a JSON/chunk igazítást és a policytől független bináris adatot ellenőrzi. Lásd [tests/README.md](tests/README.md).

## Korábbi fontos döntések

- A GLB az Archicad **alapmotor** 3D nézetének megjelenését és forrásadatait viszi át. A Maxon/Redshift textúra- és fényességkísérletet visszavontuk; nincs kettős anyagtárolás. A szebb PBR-anyagokat a viewer Material Workbenchében lehet hozzáadni. Ne vezessünk be modellre szabott, bizonytalan anyagheurisztikát.
- A veszteségmentes, primitíven belüli pontos csúcsösszevonás és az alkalmas primitívek 16 bites indexei a v1.1.0 óta a production exporter részei. A geometria részletességét szándékosan nem egyszerűsítjük, és nincs Draco vagy veszteséges kvantálás.
- Az Archicad textúra nélküli felületeinek UV-sűrűsége nem egységes. Az exporter UV0 általános átírása emiatt nem biztonságos. A viewer Workbench az új textúrák méteres leképezését külön `TEXCOORD_1` adatokkal oldja meg; az eredeti `TEXCOORD_0` megmarad.
- Az átlátszó üveg `KHR_materials_transmission`/`KHR_materials_ior` anyag. Az ablakfények forrásadatai `scenes[0].extras.dropView.windowLights` alatt vannak, nem glTF punctual lámpák. A nézegető fényerejét az exporter nem állítja.

## Kódtérkép és ellenőrzés

| Fájl | Szerep |
| --- | --- |
| `Src/GlbExporter.cpp` | Archicad 3D modell beolvasása, exportdialógus, csoportosítás, anyag- és fényadatok. |
| `Src/GlbWriter.hpp`, `Src/GlbWriter.cpp` | GLB adatmodell, glTF JSON és BIN szerializálása. |
| `Src/MaterialConversion.hpp`, `Src/TextureCoordinates.hpp`, `Src/SurfaceNormals.hpp` | Színtér, UV és normál konverzió. |
| `tests/` | DevKit nélküli C++ tesztek, writer-fixture és GLB-validátor. |
| `VERSION`, `RELEASE_NOTES.md`, `RELEASING.md`, `.github/workflows/` | Verzió, kiadási leírás és automatizált csomagolás. |

Portable tesztek:

```sh
cmake -S . -B build-tests -DDROPVIEW_TESTS_ONLY=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests --build-config Release --output-on-failure
```

Az add-on buildjéhez az adott Archicad főverzióhoz való DevKit kell. A macOS példa és korlátok a [README](README.md#building-for-archicad-2830-on-macos) fájlban vannak. Új publikus verzióhoz a `VERSION` és `RELEASE_NOTES.md` módosítandó, majd a [Publish Full Release](RELEASING.md) workflow építi és publikálja mind a hat változatot. A Windows APX jelenleg nincs Authenticode-aláírva.

## Következő chat kezdete

Javasolt első kérés: „Olvasd el a `PROJECT_HANDOFF.md` és `README.md` fájlt, nézd meg a `git status -sb` eredményét és a legutóbbi release-t, majd a jelenlegi kód alapján folytasd az adott feladatot.” Nincs kijelölt következő exporter-fejlesztés; az új feladatot a felhasználó fogja megadni.
