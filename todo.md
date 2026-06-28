# Driver para escaner USB WCM HIDKeyBoard

## Objetivo

Crear un controlador especifico para el escaner USB:

- USB VID/PID: `34eb:1502`
- Nombre HID: `WCM HIDKeyBoard`
- Serial observado: `00000000011C`
- Evento observado: `/dev/input/event6`

El problema confirmado es que el escaner escribe URLs como teclado HID y el sistema interpreta mal caracteres de URL. Ejemplo:

```text
Esperado: http://www.example.com
Actual:   httpÑ--www.example.com
```

No se debe cambiar el teclado principal del sistema. El teclado Logitech debe seguir usando layout espanol.

## Decision tecnica inicial

La primera implementacion debe ser un driver de espacio de usuario basado en `evdev` + `uinput`, no un modulo kernel.

Motivos:

- Es reversible y no requiere cargar codigo en kernel.
- Puede filtrar solo `34eb:1502`.
- Puede capturar el lector con `EVIOCGRAB` para que el evento roto no llegue a la aplicacion.
- Puede emitir una secuencia corregida mediante un dispositivo virtual.
- Es mas facil de depurar con logs y pruebas.

Un modulo kernel solo se considerara si el enfoque de espacio de usuario no puede interceptar o reemitir correctamente en el entorno grafico real.

## Fase 0 - Inventario y restricciones

- [ ] Confirmar si el sistema esta en X11 o Wayland con `echo $XDG_SESSION_TYPE`.
- [ ] Confirmar ruta estable del dispositivo:

```bash
udevadm info /dev/input/event6
ls -l /dev/input/by-id /dev/input/by-path 2>/dev/null
```

- [ ] Confirmar permisos de lectura sobre `/dev/input/event6`.
- [ ] Confirmar permisos para crear dispositivos virtuales con `/dev/uinput`.
- [ ] Guardar salida de referencia:

```bash
lsusb
lsusb -v -d 34eb:1502
udevadm info /dev/input/event6
```

## Fase 1 - Captura de eventos crudos

Estado: completada para la URL base. Existe `scanner-dump`, lee `EV_KEY` y reconstruye texto con layout US.

- [x] Crear herramienta minima `scanner-dump` para leer eventos `EV_KEY` desde `/dev/input/event6`.
- [x] Autodetectar el lector por vendor/product `34eb:1502` cuando no se pasa `eventX`.
- [x] Escanear `http://www.example.com` con `sudo build/scanner-dump`.
- [x] Registrar keycodes recibidos, no caracteres ya interpretados por XKB.
- [x] Confirmar si el escaner envia `KEY_SEMICOLON` para `:` y `KEY_SLASH` para `/`, o alguna variante.
- [x] Confirmar sufijo de escaneo: `ENTER`, `TAB`, ninguno u otro.

Criterio de salida:

- Existe una tabla fiable de keycodes para una URL de prueba.
- Sabemos detectar inicio/fin de escaneo sin depender del texto roto.

## Fase 2 - Traductor US HID a texto

- [x] Implementar tabla de entrada para layout US HID.
- [x] Convertir eventos del lector a caracteres logicos:

```text
KEY_H KEY_T KEY_T KEY_P KEY_SEMICOLON KEY_SLASH KEY_SLASH ...
=> http://...
```

- [x] Soportar caracteres habituales de URL:

```text
a-z A-Z 0-9 : / . - _ ? = & % # + @ ~
```

- [x] Ignorar teclas modificadoras salvo `SHIFT`.
- [x] Bufferizar hasta sufijo (`ENTER`/`TAB`) o timeout corto.
- [x] Validar que el resultado parece URL antes de reemitir.

Criterio de salida:

- El traductor produce `http://www.example.com` internamente aunque el escritorio muestre `httpÑ--...` sin el driver.

## Fase 3 - Intercepcion exclusiva del lector

Estado: completada para la URL base. `scanner-bridge` aplica `EVIOCGRAB` al lector detectado.

- [x] Abrir `/dev/input/eventX` del dispositivo `34eb:1502`.
- [x] Aplicar `EVIOCGRAB` para que el escaneo roto no llegue al escritorio.
- [x] Validar `scanner-bridge --dry-run`: no escribe nada en el editor y muestra `DRY-SCAN: "http://www.example.com"`.
- [x] Gestionar desconexion/reconexion USB.
- [x] Reintentar si cambia de `/dev/input/event6` a otro `eventX`.
- [x] Detectar el dispositivo por `ID_VENDOR_ID=34eb` e `ID_MODEL_ID=1502`, no por numero de evento.

Criterio de salida:

- Al ejecutar el servicio, escanear ya no escribe `httpÑ--...` directamente.

## Fase 4 - Emision corregida

Estado: completada para la URL base. `scanner-bridge` crea un teclado virtual `escanner corrected keyboard` y emite combinaciones pensadas para layout espanol.

- [x] Evaluar dos modos de salida:

```text
modo uinput-es: emitir keycodes que producen los caracteres correctos con layout espanol global.
modo text-backend: usar backend grafico/clipboard si uinput no sirve en Wayland.
```

- [x] Empezar por `uinput-es` para no depender de X11.
- [x] Crear dispositivo virtual `escanner-corrected`.
- [x] Emitir caracteres corregidos y sufijo final (`ENTER` si procede).
- [x] Mantener el teclado Logitech sin cambios.
- [x] Validar emision real con `sudo build/scanner-bridge`.
- [x] Confirmar que `http://www.example.com` llega correctamente al editor.
- [x] Descartar por defecto escaneos incompletos o URLs invalidas, por ejemplo `https:/frag`.
- [x] Anadir retardos configurables a la emision por `uinput`.
- [x] Separar lectura y emision con una cola/hilo para no bloquear el drenaje de `evdev`.
- [x] Dejar por defecto `key-delay-us=1000` y `char-delay-us=3000`.
- [x] Implementar perfiles de salida seleccionables con `--output-layout es|us|fr|de`.
- [ ] Validar en escritorio real los perfiles `us`, `fr` y `de`.
- [ ] Ampliar/validar tabla de salida con URLs que contengan `?`, `=`, `&`, `%`, `_`, `+`, `#`, `@`, `~`.

Criterio de salida:

- En un editor de texto, escanear `http://www.example.com` escribe exactamente `http://www.example.com`.

## Fase 5 - Servicio systemd

- [x] Crear binario instalable.
- [x] Crear regla udev para permisos del lector o grupo dedicado.
- [x] Crear regla udev para permisos de `/dev/uinput` o definir ejecucion como root via systemd.
- [x] Instalar `/etc/modules-load.d/escanner.conf` para cargar `uinput` en boot.
- [x] Crear unidad systemd:

```text
escanner.service
```

- [x] Arrancar automaticamente en boot con `systemctl enable --now escanner.service`.
- [x] Registrar logs con `journalctl -u escanner.service`.
- [ ] Probar servicio instalado en la maquina real.

Criterio de salida:

- Tras reiniciar, el escaner funciona sin comandos manuales.

## Fase 5.5 - Publicacion GitHub

- [x] Reescribir `README.md` con contexto, dispositivo afectado y uso.
- [x] Documentar alcance por idiomas/layouts.
- [x] Anadir `.gitignore` para no subir binarios de `build/`.
- [x] Anadir licencia MIT.
- [x] Documentar instalacion `systemd`/`udev`.
- [x] Crear release notes iniciales.
- [x] Decidir nombre final del repositorio: `barcode-scanner-layout-bridge`.

## Fase 6 - Pruebas de aceptacion

- [ ] Probar URLs:

```text
http://www.example.com
https://example.com/a?x=1&y=2
https://dominio.test/path_file-1?q=a+b%20c#frag
```

- [x] Probar URL base `http://www.example.com`.
- [ ] Probar que el teclado Logitech sigue escribiendo en espanol.
- [ ] Probar desconectar/reconectar el lector.
- [ ] Probar escaneos rapidos consecutivos.
- [ ] Probar que el servicio no captura otros teclados.
- [ ] Probar `sudo make install`, `systemctl enable --now escanner.service` en la maquina real.

## Fase 7 - Alternativa kernel, solo si hace falta

- [ ] Documentar por que falla espacio de usuario.
- [ ] Evaluar `hid-wcm-barcode` como modulo HID quirks.
- [ ] Implementar remapeo minimo solo para `34eb:1502`.
- [ ] Evitar cambiar el comportamiento de otros HID keyboard.
- [ ] Probar carga/descarga segura del modulo.

Esta fase queda bloqueada hasta demostrar que `evdev` + `uinput` no es suficiente.
