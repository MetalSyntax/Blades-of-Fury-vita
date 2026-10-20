# Blades of Fury — Progreso del Port (iOS Mach-O)

Estado al 2026-10-18: **implementación completa en PC, compilado y empackado. Solo resta probar y depurar en consola física** (gate de cada fase se valida ahí con el flujo de triage del plan).

- [x] **0. Repo y herramientas**: IPA extraído, binario analizado, `game_app` symlink, scaffolding creado.
  - [x] IPA extraído y binario confirmado (cryptid 0, thin ARM, 6010 KB, 803 imports / 41991 palabras de rebase).
  - [x] `.psvita-toolkit.json`, CMakeLists con toolchain Vita, LiveArea y tools.
  - [x] `psvita-toolkit doctor`: VITASDK, CMake, make y pdb tools OK (avisos menores: ninja/psp2cgc/PVRTexTool sin instalar, Docker daemon apagado).
- [x] **1. Loader mínimo (código)** — GATE en consola: el log llega al bucle principal / inicialización de motor.
  - [x] Carga de segmentos con `macho_load` (slide `0x98000000`, RX por kubridge).
  - [x] Aplicación de `rebase.bin` (staged en `ux0_data/bladesoffury/rebase.bin`).
  - [x] Resolución de imports (thunks con trace, unimpl-thunk con silenciado y `ml_align_call`).
  - [x] `macho_run_inits()` para `__mod_init_func` + salto a `_main`/`_SDL_main`/`_startClet`.
  - [x] Fix de compilación: campos de `struct section` (`flags & 0xFF` como tipo, `reserved1` como índice indirecto).
- [x] **2. Render (código)** — GATE en consola: primer frame / pantalla de inicio visible.
  - [x] vitaGL vendorizada con `SOFTFP_ABI=1` (compiló OK vía ExternalProject).
  - [x] FBO virtual 320x480 → presentación escalada/rotada a 960x544 (`gles_present`).
  - [x] Texturas PNG CgBI (`cgbi.c`, verificado contra referencia con `tools/cgbi_check.py`).
  - [x] `SKP_SPLASHSCREEN`/`NO_SPLASHSCREEN=1` para evitar el freeze de splash.
- [x] **3. Input (código)** — GATE en consola: controles funcionales in-game.
  - [x] Multitouch frontal mapeado a coordenadas lógicas (`touch_point.gx/gy`).
  - [x] Botones físicos (D-Pad, Cross, Circle, Square, Triangle, Triggers) vía `SceCtrl`.
- [x] **4. Texto y fuentes (código)** — GATE en consola: diálogos y menús legibles.
  - [x] FreeType enlazado y atlas en el mini-runtime ObjC (`objc.c`).
- [x] **5. Audio (código)** — GATE en consola: música y efectos activos.
  - [x] AudioUnit RemoteIO/AudioSession redirigido a `sceAudioOut` (`coreaudio.c`).
- [x] **6. Guardado y LiveArea (código)** — GATE en consola: progreso persistente y LiveArea completa.
  - [x] Carpeta `ux0:data/bladesoffury/saves/` creada al boot.
  - [x] LiveArea lista en `extras/livearea/` (icon0, pic0, bg0, startup, template) empaquetada en el VPK.

## Verificación de build (PC)

- [x] `cmake -S . -B build` + `cmake --build build --parallel 10`: **OK** → `build/eboot.bin` y `build/bladesoffury.vpk` (1.0 MB).
- [x] `tools/cgbi_check.py`: PNG CgBI idénticos a referencia → OK.
- [x] `tools/printf_check.py`: `darwin_printf.c` OK.
- [x] `tools/stage_data.sh`: staging completo en `ux0_data/bladesoffury/` (Bundle + rebase.bin + saves/ + logs/).
- [x] `psvita-toolkit clean-junk`: 1073 `._*` de macOS eliminados.

## Pendiente (solo consola física)

1. Desplegar: `psvita-toolkit deploy --eboot --yes` (y datos una vez con `deploy --data`).
2. Iterar por fase siguiendo los GATE 1→6 (log a `ux0:/data/bladesoffury/logs/`, crash con `analyze`).
3. Avisos opcionales sin bloquear: ninja (build más rápido), psp2cgc (validar shaders), Docker Desktop (Ghidra si hace falta decompilar).

## Triage de corridas físicas

- **Corrida 1 (2026-10-18, log_001.log)** — Crash inmediato tras reload: `FATAL Failed to open ux0:data/bladesoffury/game_app/BladesOfFury`. Causa real: `BUNDLE_PATH` en `hle.h` apuntaba a `game_app/` (nombre del symlink en PC) en vez de `BladesOfFury.app/` (lo que sube `stage_data.sh`). Corregido a `BladesOfFury.app/` y recompilado (`build/eboot.bin` nuevo listo para desplegar). Fase 1 avanza hasta el open del Mach-O.

- **Corrida 2 (2026-10-19, log_002.log)** — `FATAL Mach-O does not contain __TEXT or __DATA segments`. Causa real: `segment_command` en `macho.c` no tenía `cmd`/`cmdsize`, así que `segname` se leía 8 bytes antes (nunca coincidía). Corregido. En la misma revisión: los segmentos se reservaban con `sceKernelAllocMemBlockForVM` (dirección aleatoria) pero se escribían en `MACHO_SLIDE + vmaddr` (sin mapear → crash seguro). Ahora se usa `kuKernelAllocMemBlock` con vbase fija (RX para __TEXT, RW para __DATA), con fatal si la base no coincide.

- **Corrida 3 (2026-10-19, log_003.log)** — `__TEXT`/`__DATA` mapeados OK en `0x98001000`/`0x9839D000` (fix de corrida 2 validado). Luego `FATAL Thunk arena exhausted`: el bloque de thunks se pedía con `sceKernelAllocMemBlockForVM`, que falla y dejaba `thunk_head = NULL`. Ahora es un bloque RX de kubridge en vbase fija tras `__DATA` (`0x98466000`), con fatal si falla. Revisión del resto del loader en la misma pasada (todos confirmados contra el binario):
  - Las 5922 relocaciones externas no se aplicaban (vtables, `isa` de CFString, superclases ObjC) → ahora se resuelven (addend in-place = 0, verificado).
  - Doble slide: los slots `INDIRECT_SYMBOL_LOCAL` y `__mod_init_func` ya vienen en `rebase.bin`, pero el loader les volvía a sumar `MACHO_SLIDE`. Eliminado.
  - Los thunks se cachean por símbolo; `rebase.bin` ausente ahora es fatal.
  - Plan revisado: `objc.c` era el de Inotia2 y no podía arrancar este juego (`UIApplicationMain` era un stub que devolvía 0 → pantalla negra). Se implementó la fase 1.5 (ver PORTING_PLAN).

- **Corrida 4 (2026-10-19, log_004.log + psp2dmp)** — Carga, binding (5922 extrel, 326 unimpl, 478 thunks) OK. `Invalid rebase.bin header` y luego Prefetch abort en PC `0x24bc` (= primer `__mod_init_func` sin deslizar). Causa real: `rebase.bin` en la consola pesaba **0 bytes**; la subida de datos anterior se cortó y además faltaban/estaban incompletos 187 de 351 archivos del bundle (`textures`, `title.m4v`, `window`, `.bar`, `.wav`...). Fixes: redeploy completo de datos; `rebase.bin` corrupto/truncado ahora es fatal con mensaje claro; `tools/deploy_data.py` verifica el tamaño remoto tras cada subida y aborta si no coincide.

- **Corrida 5 (2026-10-19, log_005.log + psp2dmp)** — Datos completos, rebase aplicado (41991). Data abort en `0x98002394` (`__Z41__static_initialization_and_destruction_0ii`, primer constructor): `str r2,[r3]` con `r3 = 0x00461f28` (`__bss`) sin deslizar. Causa real: el binario **no es PIE** y no trae relocaciones locales; sus literal pools en `__text` (`ldr Rd,[pc,#imm]`) tienen direcciones absolutas y `macho_prep.py` excluía `__text`. Fix: pasada de literales de código (ARM `ldr pc` ±imm12 y Thumb `ldr pc` imm8, literal dentro de su función, destino validado; para código se aceptan interiores de `__bss`/`__common`/`__cstring`/`__gcc_except_tab` = bools, cadenas fusionadas por sufijo, LSDAs SjLj). +12388 palabras → `rebase.bin` 54379. Solo se resubió `rebase.bin`.

Nota importante sobre builds: `psvita-toolkit build` copia el fuente a un directorio temporal que excluye `vendor/` (vitaGL), lo que rompe el ExternalProject. Compilar directo con `cmake -S . -B build` usando `PATH=$VITASDK/bin:$PATH`.

## 2026-10-19 — Migración a la arquitectura de Zombie-Infection-vita (sin corrida de consola aún)

El loader de Zombie Infection (mismo motor iOS 3 de Gameloft, ya en gameplay) se trajo a Blades:

- **Runtime ObjC 2.0 + UIKit** (`objc.c`, `objc_rt.h`, `uikit.c`, `asm.S`): reemplaza el despacho por nombre de selector (`objc_msgSend_native`). Clases host bajo demanda, caché de métodos, NSString/NSArray/NSDictionary/NSData/NSFileManager/NSUserDefaults reales, NSTimer reciclado, NSThread. Adaptado: delegate `MedievalCombatAppDelegate`, orientación por `statusBarOrientation` (ConvertCoordP desensamblado: 3 → (y, 320-x), 4 → (480-y, x)), bundle id/versión `com.gameloft.BladesOfFury` 1.1.3.
- **`gles.c`**: FBOs/renderbuffers reales de vitaGL (render-to-texture funciona) salvo el drawable del CAEAGLLayer; solo el primer adjunto de depth por framebuffer; conversión de tipos de array que vitaGL no acepta (GL_BYTE, color GL_FIXED, también en VBO); blit que guarda/restaura todo el estado fijo; pantalla completa 960x544 sin estirar el HUD; entradas float (`glOrthof`, `glFrustumf`, `glLightfv`...).
- **`darwin.c`**: rutas con `dir/../` colapsado y `Documents/` → `saves/`; `stat`/`dirent` con layout Darwin; `chdir` valida el directorio. Faltaban en la tabla (implementadas pero no registradas): `chdir`, `getcwd`, `stat`, `opendir/readdir/closedir`, `printf`, `snprintf`, `strncmp`, `strchr`... Agregados además: `fprintf/vfprintf`, `remove/rename/unlink`, `__stdoutp`, rwlocks, `pthread_attr_*`, softfp `__divdf3vfp`/`__fixunssfsivfp`/`__floatundisf`/`__unordsf2vfp`, `setjmp/longjmp` con el jmp_buf Darwin (asm, enlazados sin thunk).
- **OpenAL** (`coreaudio.c` + `openal` en CMake): 20 imports `al*`/`alc*` a openal-soft.
- **`macho.c`**: `macho_sym` por búsqueda binaria; `macho_resolve` (import o símbolo del guest).
- **Imports sin resolver: 239 → 60** (AudioQueue/AudioFile, CGImage/UIGraphics de imagen, CFArray, CC_MD5, SCNetworkReachability callbacks, personalities SjLj). Quedan como stubs que logean y devuelven 0.
- `tools/test_darwin_path.sh`: OK. Build PC: OK (`build/eboot.bin`).

**Desplegado:** no. Próxima corrida: subir `build/eboot.bin` (los datos no cambian) y revisar el log por `objc: ... not implemented` y por imports `unimpl`.

## 2026-10-19 — Corrida 7 (log_007 + psp2dmp): data abort en el constructor 209/378

- Runtime nuevo OK hasta los constructores (14 clases host, 53 imports sin implementar).
- **Crash:** `str r2,[r3]` en `0x145B4` (`__static_initialization_and_destruction_0`, Thumb) con
  `r3 = 0x30463EC8`. **Causa real:** el código Thumb es PIC (`ldr r3,[pc,#0x48]` + `add r3, pc`) y
  la pasada de literales de la corrida 5 deslizó el offset `0x0044F914` porque parecía una
  dirección de `__bss`: `0x9844F914 + pc` desborda.
- **Fix (`tools/macho_prep.py`, detección de Zombie-Infection-vita):** se sigue el registro cargado;
  si se usa con pc (Thumb `add Rt, pc`; ARM `add Rd, pc, Rt` / `ldr|str Rd, [pc, Rt]`) es PIC y no
  se rebasea. Literales absolutos 12388 → 10903 (14820 cargas PIC excluidas).
- **Mejora:** tablas de switch non-PIC (`ldrls pc, [pc, Rm, lsl #2]`) no se rebaseaban: +1052
  entradas (habrían crasheado en el primer `switch`). `rebase.bin` 54379 → 53946 palabras.
- `tools/test_macho_prep.py`: regresión con los casos de las corridas 5 y 7 + una jump table. OK.
- Log `global constructors: N done` al terminar los constructores.

**Desplegado:** sí (`eboot.bin` + `rebase.bin`).

## 2026-10-19 — Corrida 8 (log_008 + psp2dmp): data abort en zlib `inflate_table`

- Fix de corrida 7 validado: 378 constructores OK, `UIApplicationMain`, `applicationDidFinishLaunching:`,
  lectura de `data2d.bar` / `config.bar`.
- **Crash:** `ldrh r3,[r2,r4]` en `0x14FA6E` (`_inflate_table`, Thumb): `count[lens[sym]]++` con
  `lens[sym] = 0xED84`. **Causa real:** `rebase.bin` deslizaba el word `0x39BE80` de la tabla estática
  `_order.3008` de zlib: `{8, 7}` = `0x00070008` coincide con el inicio de
  `WfStepBase<WfMatchEndStep>::Init` (ARM). `order[4]` pasaba a `0x9807`, así que se escribía `lens[0x9807]`
  y `lens[7]` quedaba sin inicializar. En `macho_prep.py`, los dueños descartados por mayoría de
  valores no-puntero conservaban igual sus aciertos `func`.
- **Fix (`tools/macho_prep.py`):** un dueño descartado pierde todos sus aciertos; un dueño no alineado
  a 4 (arreglo de char/short) no tiene punteros; layout explícito para `_configuration_table` de zlib
  (puntero solo en el offset 8 de cada entrada de 12). Salen exactamente 5 falsos positivos:
  `_order.3008`, `_static_ltree` (Huffman fijo), `_configuration_table` ×2 (`{8,16}`), `_k_country`
  (texto). No se agrega ni se pierde nada más. `rebase.bin` 53946 → 53941.
- `tools/test_macho_prep.py`: 8 casos (corridas 5, 7 y 8 + jump table + zlib + texto). OK.
- **Preventivo:** `CFBundleCopyBundleURL`, `CFURLCopyFileSystemPath`,
  `CFURLCreateFromFileSystemRepresentation` y `CFStringGetCString` (rutas de recursos, AudioFileOpenURL);
  `NSScanner` (`scanInteger:`, `scanString:intoString:`, `scanUpToCharactersFromSet:intoString:`,
  `isAtEnd`) y `NSCharacterSet characterSetWithCharactersInString:`, usados al arrancar.

**Desplegado:** sí (`eboot.bin` + `rebase.bin`).

## 2026-10-19 — Corrida 9 (log_009.log + psp2dmp): data abort en `strcpy` desde `_GetDeviceLanguage`

- Fix de corrida 8 validado: `_inflate_table` sin corrupción, carga de `.bar` completa, EAGL FBO (320x480), NSTimer e inicio del run loop.
- **Crash:** Data abort exception (`0x30004`) en `PC: 0x811a5f7e` (`strcpy+0x10`, `ldr.w r3, [r1], #4`) con `r1 = 0x00000000` (puntero nulo), con `LR = 0x9802098c` (`_GetDeviceLanguage+0x58`).
- **Causa real:** El binario Mach-O invoca `_GetDeviceLanguage(char *out_buf)` (offset `0x00020934`, llamado desde `0x00133190` en `CEnvironment::SetLanguage`). La implementación guest hace:
  `[[[NSUserDefaults standardUserDefaults] objectForKey:@"AppleLanguages"] objectAtIndex:0] UTF8String`
  y luego `strcpy(out_buf, utf8_str)`.
  En `source/objc.c`, `ud_standard` devolvía un diccionario `defaults_obj` vacío. Al no existir la clave, `objectForKey:@"AppleLanguages"` devolvía `nil`, `objectAtIndex:0` devolvía `nil`, `UTF8String` devolvía `0x0` y `strcpy(out_buf, NULL)` provocaba el data abort.
- **Fixes implementados:**
  1. En `source/objc.c`: `ud_standard` y `loc_preferred_languages` ahora obtienen el idioma configurado en la PS Vita mediante `sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, ...)` (mapeado a `"en"`, `"es"`, `"fr"`, `"de"`, `"it"`, `"ja"`) y pueblan `"AppleLanguages"` en `defaults_obj` como un `NSArray` con ese código.
  2. Aliases agregados en `NSDictionary`: `stringForKey:` y `arrayForKey:` despachan a `d_object_for_key`.
  3. En `source/patch.c`: `macho_hook("_GetDeviceLanguage", hook_GetDeviceLanguage)` intercepta la rutina del binario copiando directamente el código de idioma al buffer con logging defensivo.
- Build PC: OK (`build/eboot.bin`).

## 2026-10-19 — Corrida 10 (log_010.log): freeze en logo de Gameloft (error OpenAL a004 + dirent truncado)

- Fix de corrida 9 validado: `_GetDeviceLanguage` interceptado con éxito (`returning 'es' (Vita system lang: 3)`), run loop avanza y se muestra el logo de Gameloft en pantalla.
- **Síntoma:** El juego se congela en el logo de Gameloft sin pasar a menús / gameplay, generando miles de mensajes de error continuos en el log.
- **Causas reales identificadas:**
  1. **Truncamiento masivo de nombres de archivo en `readdir` (offset de `d_name` desfasado en 13 bytes):**
     - Al escanear `BladesOfFury.app` con `opendir`/`readdir`, el binario ejecutaba `adds r4, #0x15` (offset 21) para acceder a `d_name`.
     - En `source/darwin.c`, `darwin_dirent` usaba la definición pre-INODE64 antigua donde `d_name` estaba en el offset 8.
     - En consecuencia, cada nombre de archivo leído se truncaba en 13 caracteres (ej. `chinagirl_suit2.epk` se leía como `uit2.epk`, `beast_suit2.epk` como `2.epk`), haciendo que todas las llamadas `stat()` a los paquetes `.epk` y assets fallaran (`stat -> -1`).
  2. **Bucle infinito en `_SoundEngine_Initialize` por error OpenAL `0xA004`:**
     - En `0x00134dc8`, el motor de sonido nativo comprueba `alGetError()` inmediatamente después de `alcOpenDevice(NULL)`.
     - Según la especificación de OpenAL-soft, `alGetError()` sin un contexto activo retorna `AL_INVALID_OPERATION` (`0xA004`), mientras que la implementación de Apple en iOS retornaba `AL_NO_ERROR` (0).
     - Al recibir `0xA004`, la función fallaba imprimiendo `Error opening output device: a004` sin llegar a crear el contexto (`alcCreateContext`), y el bucle del juego reintentaba la inicialización en cada ciclo indefinidamente.
- **Fixes implementados:**
  1. En `source/darwin.c`: se actualizó `darwin_dirent` con la estructura real de Darwin 64-bit (`__DARWIN_STRUCT_DIRENT64`: `d_ino` 8B, `d_seekoff` 8B, `d_reclen` 2B, `d_namlen` 2B, `d_type` 1B, `d_name` en offset 21 = 0x15 con `#pragma pack(push, 4)`). Ahora `readdir` entrega los nombres completos sin truncar.
  2. En `source/coreaudio.c`: se implementó el wrapper `wrap_alGetError()`, que devuelve `AL_NO_ERROR` si no hay contexto activo (`!alcGetCurrentContext()`), permitiendo que `_SoundEngine_Initialize` proceda exitosamente a crear el contexto y activar el audio.
- Build PC: OK (`build/eboot.bin` y `build/bladesoffury.vpk`).

## 2026-10-19 — Corrida 12 (log_012.log + psp2dmp): Undefined instruction en `Matrix4Mul` (VFPv2 vector mode) + Pantalla completa + Controles físicos

- Fix de corrida 11 validado: Menú principal completamente operativo, sin crash al omitir AudioQueue, renderizado funcional. El juego avanzó hasta el selector de personajes.
- **Crash en selector de personajes:** Undefined instruction exception (`0x30002`) en `PC: 0x982eaf3c` (`Matrix4Mul(float const*, float const*, float*)+0x20`, `vmul.f32 s24, s8, s0`).
- **Causa real del crash:**
  - Gameloft compiló las funciones de álgebra lineal (`Matrix4Mul`, `Matrix4Vector3Mul`, `Matrix4Vector3MulAndRot`) para ARMv6 VFPv2 habilitando **modo vectorial en hardware** (`FPSCR.LEN = 3`, `0x00030000`, longitud 4).
  - En la arquitectura ARM Cortex-A9 MPCore de la PS Vita (ARMv7-A), el modo vectorial VFP por hardware está obsoleto/no soportado y provoca inmediatamente una excepción de instrucción no definida (`0x30002`) al ejecutar la primera instrucción VFP vectorial.
  - Se confirmó mediante escaneo exhaustivo de opcodes en todo el binario que solo existen 3 funciones en todo el juego que activan `FPSCR.LEN = 3`:
    1. `__Z10Matrix4MulPKfS0_Pf` (`0x002eaf1c`)
    2. `__Z17Matrix4Vector3MulPKfS0_Pf` (`0x002eafac`)
    3. `__Z23Matrix4Vector3MulAndRotPKfS0_PfS0_S1_f` (`0x002eaff4`)
- **Fixes implementados:**
  1. **Hooks de matrices en [source/patch.c](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/patch.c):**
     - Se implementaron reemplazos en C escalar estándar para `Matrix4Mul` (multiplicación 4x4 por columnas), `Matrix4Vector3Mul` (transformación afín con w=1), y `Matrix4Vector3MulAndRot` (transformación afín + rotación 3x3 escalada por `scale`).
     - Se registraron en `patch_init()` mediante `macho_hook`.
  2. **Eliminación de barras negras (Pantalla completa 960x544):**
     - En [source/gles.c](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/gles.c): se cambió `WIDE_K` de `816/960` a `1.0f` y `wide_target()` a retornar `0`. Esto elimina la compresión artificial de la matriz de proyección y del scissor que dejaba 72 px negros a cada lado, permitiendo que el FBO 320x480 se extienda limpiamente sobre la resolución nativa 960x544 de la consola.
     - En [source/input.c](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/input.c): se ajustó el mapeo táctil directo a pantalla completa (`touches[i].x * 480 / 960`, `touches[i].y * 320 / 544`), manteniendo alineación 1:1 entre pantalla táctil y gráficos.
  3. **Configuración de controles físicos ([source/patch.c](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/patch.c), [source/input.h](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/input.h), [source/uikit.c](file:///Volumes/TOSHIBA%20EXT/PSVITA%20Develop/Blades-of-Fury-vita/source/uikit.c)):**
     - Se aumentó la capacidad de búfer táctil a `MAX_TOUCHES = 16`.
     - En `patch_input()` se implementó la inyección de toques sintéticos mapeando los botones físicos y el stick analógico a las coordenadas exactas del HUD de Blades of Fury (extraídas de `CJoyStick::Init`):
       - **Stick analógico izquierdo & Cruceta (D-Pad):** Mapeado al D-Pad virtual centrado en `(80, 250)` con radio 60 px.
       - **Cuadrado ($\square$):** Ataque Horizontal A (`_GAME_ATTACK_A_POS`: `361, 255`).
       - **Triángulo ($\Delta$):** Ataque Vertical B (`_GAME_ATTACK_B_POS`: `410, 206`).
       - **Círculo ($\bigcirc$) & R1:** Magia / Especial / Patada C (`_GAME_ATTACK_C_POS`: `430, 157`).
       - **Cruz ($\times$) & L1:** Guardia / Defensa D (`_GAME_ATTACK_D_POS`: `430, 270`).
       - **Start:** Botón de Pausa (`455, 25`).
- Build PC: OK (`build/eboot.bin` y `build/bladesoffury.vpk`).

## 2026-10-19 — Corrida 13 (log_013.log + psp2dmp): Data abort en `CTrailSceneNode::update` por falso positivo de rebase

- Fix de corrida 12 validado: Selector de personajes superado sin crash (los hooks de `Matrix4Mul` funcionan perfectamente), pantalla completa 960x544 activa sin barras negras, carga del nivel forestal (`m_stage_forest.m4a`). **¡El juego entra al in-game 3D!**
- **Crash in-game:** Data abort exception (`0x30004`) en `PC: 0x9805851c` (`CTrailSceneNode::update()+0xf0`, `ldr r2, [r5, r3]` con `r3 = 0x98004804` y `r5 = 0x825a4f44`), llamado desde `LR: 0x98031528` (`CHero::ProcessTrail(int)`).
- **Causa real del crash:**
  - En `CTrailSceneNode::update`, la instrucción en `0x58518` es `ldr r3, [pc, #0x408]`, que lee el literal en `0x58928`.
  - El valor original de este literal en el binario es `0x00004804` (18436 bytes), que corresponde al desplazamiento (offset) de un campo dentro del objeto `CTrailSceneNode` para la siguiente instrucción `0x5851c: ldr r2, [r5, r3]`.
  - En la tabla de símbolos del binario, la función interna generada por GCC `__Z41__static_initialization_and_destruction_0ii` comienza casualmente en la dirección exacta `0x00004804`.
  - `macho_prep.py` consideraba que cualquier valor que coincidiera con el inicio de un símbolo en `__text` era un puntero a función que debía ser rebaseado. Como resultado, deslizó el literal `0x58928` de `0x00004804` a `0x98004804`.
  - Al ejecutar `ldr r2, [r5, r3]`, el puntero base en heap (`0x825a4f44`) más el falso puntero (`0x98004804`) desbordó los 32 bits (`0x11a5a9748`), provocando un crash inmediato por acceso a memoria inválida tan pronto como los personajes realizaban un golpe y activaban la estela de espada (trail).
- **Fix implementado:**
  1. En `tools/macho_prep.py`: se excluyeron explícitamente los símbolos internos `static_initialization` de `func_entry`, ya que son funciones auxiliares de inicialización estática de GCC cuya dirección nunca es tomada en C/C++.
  2. Al regenerar `rebase.bin`, se eliminó exactamente 1 sola palabra de rebase: el falso positivo en `0x58928` (53941 → 53940 palabras).
  3. Se actualizó la suite de regresión `tools/test_macho_prep.py` agregando el caso de la corrida 13 (9/9 casos pasan OK).
  4. Se regeneró `ux0_data/bladesoffury/rebase.bin`.





