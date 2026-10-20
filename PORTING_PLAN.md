# Blades of Fury (iOS) → PS Vita

Port de Blades of Fury (iOS, Mach-O arm) a PS Vita con loader de Mach-O propio.
Basado en la arquitectura probada de `ios-loader-boilerplate` (caso de estudio Inotia2-vita).

## Lo confirmado del binario (2026-10-18)

- Ejecutable: `BladesOfFury`, thin/fat **arm**.
- Cifrado FairPlay: **cryptid 0** (descifrado, listo para ejecución).
- Tamaño: 6010 KB.
- Bundle ID: `com.gameloft.BladesOfFury`.
- Paquete original: `Blades of Fury_v.1.1.3.ipa`.
- App bundle: `game_app` → `/Volumes/TOSHIBA EXT/PSVITA Develop/Blades-of-Fury-vita/bladesoffury_extract/Payload/BladesOfFury.app`.
- Palabras de rebase (`rebase.bin`): **54379** (incluye 12388 literales de código: el binario **no es PIE** y no trae relocaciones locales).
- Imports únicos detectados: **803** (GL: 283, Audio: 98, ObjC/CF: 288, POSIX/libc: 31).
- Relocaciones externas: **5922** (5332 vtables → `___cxa_pure_virtual`, 114 CFString `isa`, clases/metaclases ObjC, 148 símbolos C++ definidos en el propio binario). Addend in-place siempre 0.
- Motor: **Irrlicht (Gameloft)** + Bullet. ObjC propio: 21 clases (`MedievalCombatAppDelegate`, `EAGLView`, ...). `NSMainNibFile = MainWindow`, landscape right.

## Flujo de arranque del juego (desensamblado)

`main` → `UIApplicationMain(argc, argv, nil, nil)` → `MainWindow.nib` crea `EAGLView` (`initWithCoder:`) y el delegate (outlets `window`/`glView`)
→ `applicationDidFinishLaunching:` → `[glView gameStart]` → `initGame` (`irr::createDevice`, `CWorld::Init`) + `NSThread detachNewThreadSelector:threadRoutineFunc`
→ `applicationDidBecomeActive:` → `[glView startAnimation]` → `NSTimer` → `timerRoutineFunc` → `GameLoop` → `presentRenderbuffer:`.
Toques: `touchesBegan/Moved/Ended:withEvent:` con fast enumeration de `NSSet`, `locationInView:` (stret) y `ConvertCoordP:` (rota solo si `statusBarOrientation` es 3/4).

## Reglas Críticas de ABI de iOS en PS Vita

1. **`r9` temporal**: En ARMv6/v7 iOS, `r9` es usado como temporal por el compilador y no se preserva. Compilar con `-ffixed-r9` y usar `call_guest` al llamar al juego.
2. **Convención SoftFP**: Floats en registros enteros (`r0..r3`). `vitaGL` vendorizada con `SOFTFP_ABI=1` obligatoria para no corromper la proyección en libgxm.
3. **Alineación de pila a 4 bytes**: El juego asume pila alineada a 4 bytes. Todo import/hook llamado desde el binario entra por `ml_align_call` (asm.S), que realinea a 8 bytes AAPCS copiando argumentos de stack.
4. **printf de Darwin**: Enteros y dobles de 64 bits viajan en palabras de 32 bits contiguas sin alinear; soportar `%@` de Objective-C (`darwin_printf.c`).

## Estructura en la Vita (`ux0:data/bladesoffury/`)

```
ux0:data/bladesoffury/
  BladesOfFury.app/   bundle del IPA limpio (ejecutable + recursos)
  rebase.bin         punteros absolutos reubicados (MACHO_SLIDE = 0x98000000)
  saves/             partidas guardadas (Documents/ de iOS)
  logs/              log_001.log .. log_999.log rotativos
```

## Arquitectura del Loader

| Archivo | Qué reemplaza / funcionalidad |
|---|---|
| `source/macho.c` / `asm.S` | Dyld Mach-O: carga, rebase, imports, thunks con trace, `call_guest`, `ml_align_call` |
| `source/darwin.c` | libc/libgcc/pthread/semáforos de Darwin con `pthread_detach` seguro contra SceUIDs |
| `source/darwin_printf.c` | Formateador variádico con soporte de dobles desalineados y `%@` |
| `source/objc.c` | Runtime ObjC: despacho a clases del juego (metadatos ObjC2) + fallback nativo, `UIApplicationMain`/run loop, `NSTimer`, `NSThread`, `UITouch`/`NSSet`, `NSString`, `UIFont` FreeType, IME |
| `source/gles.c` | Backend GLES1/2 sobre vitaGL vendorizada con FBO virtual escalado/rotado |
| `source/cgbi.c` | Soporte transparente para imágenes PNG CgBI de Apple |
| `source/coreaudio.c` | AudioUnit RemoteIO + AudioSession sobre `sceAudioOut` |
| `source/input.c` | Touch frontal + botones físicos mapeados a la capa táctil |
| `source/patch.c` | Parches y hooks específicos de Blades of Fury |

## Fases

> Estado 2026-10-19: los `[x]` de abajo son código escrito y compilado, **no** validado. Ningún GATE está cerrado en consola todavía (última corrida: log_003, fallo en la carga). Detalles en `port_progress.md`.

- [x] **0. Repo y herramientas**: IPA extraído, binario analizado, `game_app` symlink, scaffolding creado.
  - [x] IPA extraído y binario confirmado (thin ARM, cryptid 0, 6010 KB, 803 imports, rebase.bin 41991 palabras).
  - [x] Toolchain verificado (`psvita-toolkit doctor`: VITASDK, CMake, make OK).
- [x] **1. Loader mínimo (código)** — GATE en consola: el log llega al bucle principal / inicialización de motor.
  - [x] Carga de segmentos en memoria con `macho_load` (slide `0x98000000`).
  - [x] Aplicación de `rebase.bin` (staged en `ux0_data/bladesoffury/rebase.bin`).
  - [x] Resolución de imports indispensables de libc, math, pthread y runtime.
  - [x] Ejecución de inicializadores globales con `macho_run_inits()`.
  - [x] Fix de compilación: campos de `struct section` en `macho.c` (`flags & 0xFF`, `reserved1`).
- [x] **1.5 Runtime ObjC + arranque UIKit (código, 2026-10-19)** — GATE en consola: log muestra `UIApplicationMain: entering run loop` y `NSThread start: threadRoutineFunc`.
  - [x] Relocaciones externas (5922) resueltas en `macho.c`; símbolos definidos en la imagen tienen prioridad sobre imports.
  - [x] Runtime ObjC 2.0 real (base de Zombie-Infection-vita, 2026-10-19): `objc_msgSend*` en `asm.S` llaman a `objc_lookup*` (cadena de superclases + caché por clase/selector); clases host (`NSObject`, `UIView`...) creadas bajo demanda para cada `_OBJC_CLASS_$_X` importado, métodos host en tablas `host_method` (`objc.c` = Foundation, `uikit.c` = UIKit/EAGL/MediaPlayer). Tamaños host verificados contra el binario: `EAGLView` instanceStart 36 = UIView, view controllers 92 = UIViewController.
  - [x] `UIApplicationMain` (`uikit.c`) recrea `MainWindow.nib` (UIWindow + EAGLView `initWithCoder:`, outlets `window`/`glView` de `MedievalCombatAppDelegate`) y corre el run loop: toques → `UITouch` en coordenadas de vista portrait (inversa de `-[EAGLView ConvertCoordP:]`, orientación = `statusBarOrientation`, 3 por Info.plist), acelerómetro quieto, NSTimers, alertas/notificaciones diferidas, IME.
  - [ ] Métodos de categorías (`__objc_catlist`, 1 entrada: `cMP_Picker(MPMediaPickerControllerDelegate)`) no se despachan; solo se usan con el selector de música del iPod (no existe en Vita).
- [x] **2. Render (código)** — GATE en consola: primer frame / pantalla de inicio visible en consola física.
  - [x] Inicialización de vitaGL vendorizada (SOFTFP_ABI=1) y FBO virtual 320x480 → 960x544.
  - [x] Manejo de texturas PNG CgBI con `cgbi.c` (verificado con `tools/cgbi_check.py`).
  - [x] Swap de buffers con `NO_SPLASHSCREEN=1` (anti splashscreen freeze). ~60 FPS por validar en consola.
- [x] **3. Input (código)** — GATE en consola: controles funcionales in-game.
  - [x] Multitouch frontal → `UITouch` persistentes por dedo → `touchesBegan/Moved/Ended:withEvent:` de `EAGLView` (coordenadas landscape 480x320).
  - [x] Mapeo de botones físicos (D-Pad, Cross, Circle, Square, Triangle, Triggers).
- [x] **4. Texto y fuentes (código)** — GATE en consola: diálogos y menús legibles.
  - [x] FreeType sobre atlas de fuentes en el mini-runtime ObjC (`objc.c`).
- [x] **5. Audio y video (código)** — GATE en consola: música y efectos de sonido activos.
  - [x] RemoteIO / AudioSession redirigido al hilo `sceAudioOut` (`coreaudio.c`).
  - [x] Trazas de AudioSessionGetProperty para diagnóstico (`tools/printf_check.py` OK).
- [x] **6. Guardado y LiveArea (código)** — GATE en consola: progreso persistente y LiveArea completa.
  - [x] Persistencia de partidas en `ux0:data/bladesoffury/saves/` (creada en staging y al boot).
  - [x] LiveArea personalizada con `icon0.png`, `pic0.png`, `bg0.png`, `startup.png` + `template.xml` empaquetada en el VPK.

## Verificación de build (PC, 2026-10-18)

- [x] `cmake -S . -B build` + `cmake --build build --parallel 10`: OK → `build/eboot.bin` y `build/bladesoffury.vpk` (1.0 MB).
- [x] `tools/stage_data.sh`: staging completo en `ux0_data/bladesoffury/` (bundle + rebase.bin + saves/ + logs/).
- [x] `tools/printf_check.py` y `tools/cgbi_check.py`: OK.
- [x] `psvita-toolkit clean-junk`: 1073 `._*` de macOS eliminados.

## Pendiente (solo consola física)

1. Desplegar: `psvita-toolkit deploy --eboot --yes` y datos con `psvita-toolkit deploy --data`.
2. Cerrar GATE 1→6 con el flujo de triage de abajo.
3. Opcional: ninja (builds más rápidos), psp2cgc (validar shaders), Docker Desktop (Ghidra).

> Nota de build: `psvita-toolkit build` copia el fuente a un temp dir que excluye `vendor/` (vitaGL) y rompe el ExternalProject; compilar directo con `cmake -S . -B build` y `PATH=$VITASDK/bin:$PATH`.

## Iteración y Triage de Crashes

1. Staging de assets: `tools/stage_data.sh`
2. Despliegue por FTP: `python3 -I tools/deploy_data.py` (o `psvita-toolkit deploy --data`)
3. Si ocurre un crash:
   - Descargar logs: `python3 -I tools/deploy_data.py --logs`
   - Analizar volcado: `psvita-toolkit analyze logs/<dump>.psp2dmp --elf build/bladesoffury`
   - Si el PC está en `0x98xxxxxx`, restar `0x98000000` para ubicar la dirección en el Mach-O original con `tools/annotate.py`.
