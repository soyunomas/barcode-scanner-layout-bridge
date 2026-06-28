# barcode-scanner-layout-bridge

Idiomas: [English](README.md) | Español

Puente Linux en espacio de usuario para un escáner USB de códigos de barras problemático que se comporta como un teclado HID y escribe caracteres de URL con una distribución de teclado incorrecta.

Repositorio:

```text
https://github.com/soyunomas/barcode-scanner-layout-bridge
```

Este proyecto se creó para un escáner que aparece en Linux como:

```text
Bus 001 Device 004: ID 34eb:1502 WCM HIDKeyBoard
```

Con una distribución de teclado española, al escanear:

```text
http://www.example.com
```

podría producir:

```text
httpÑ--www.example.com
```

El firmware del escáner solo expone los modos de teclado `UNITED STATES` y `FRANCE`, sin modo español. Este puente captura únicamente ese escáner, traduce sus códigos de tecla HID estadounidenses sin procesar a texto de URL y vuelve a emitir las pulsaciones corregidas mediante un teclado virtual sin cambiar la distribución de teclado del sistema.

## Estado

Funciona para el dispositivo probado y una distribución de escritorio española.

Ejemplos validados:

```text
http://www.example.com
https://dominio.test/path_file-1?q=a+b%20c#frag
```

La temporización de emisión predeterminada es:

```text
--key-delay-us 1000 --char-delay-us 3000
```

El puente ahora incluye gestión de reconexión, una unidad systemd, reglas udev y perfiles seleccionables de distribución de salida:

```text
--output-layout es|us|fr|de
```

## Dispositivo probado

Hardware afectado conocido:

| Campo | Valor |
|---|---|
| USB VID:PID | `34eb:1502` |
| Cadena de fabricante | `WCM` |
| Cadena de producto | `HIDKeyBoard` |
| Nombre HID | `WCM HIDKeyBoard` |
| Serie observada | `00000000011C` |
| Interfaz | Teclado USB HID |
| Ejemplo de evento Linux | `/dev/input/event6` |

Comandos útiles para identificar el dispositivo:

```bash
lsusb
lsusb -v -d 34eb:1502
udevadm info /dev/input/event6
```

Pistas esperadas:

```text
ID_VENDOR_ID=34eb
ID_MODEL_ID=1502
ID_SERIAL=WCM_HIDKeyBoard_00000000011C
ID_INPUT_KEYBOARD=1
```

## ¿Funciona con otros idiomas?

La entrada del escáner se decodifica como eventos de teclado HID estadounidense. El lado de salida puede seleccionarse con `--output-layout`:

| Modo de salida del escáner | Distribución de escritorio | Opción | Estado |
|---|---|---|---|
| Teclado US | Distribución española | `--output-layout es` | Implementado y probado |
| Teclado US | Distribución US | `--output-layout us` | Implementado a partir del mapa XKB básico |
| Teclado US | Distribución francesa | `--output-layout fr` | Implementado a partir del mapa AZERTY básico de XKB; necesita validación con hardware real |
| Teclado US | Distribución alemana | `--output-layout de` | Implementado a partir del mapa QWERTZ básico de XKB; necesita validación con escritorio real |
| Modo francés del escáner | Cualquier distribución | No aplicable | No implementado; usa el modo `UNITED STATES` del escáner |

La distinción importante:

- Lado de entrada: el escáner se decodifica como eventos de teclado HID estadounidense.
- Lado de salida: el teclado virtual emite combinaciones de teclas que producen la URL deseada en la distribución de escritorio seleccionada.

Para admitir otra distribución de escritorio, añade una nueva función de mapeo de salida en [src/scanner_bridge.c](src/scanner_bridge.c) y regístrala en `layout_profiles`.

## Cómo funciona

`scanner-bridge`:

1. Encuentra el escáner por proveedor/producto USB `34eb:1502`.
2. Abre su dispositivo `/dev/input/eventX`.
3. Usa `EVIOCGRAB` para que las pulsaciones originales incorrectas no lleguen a las aplicaciones.
4. Convierte los eventos Linux `EV_KEY` sin procesar como entrada de teclado estadounidense.
5. Encola escaneos completos para que los escaneos rápidos repetidos no bloqueen la lectura de entrada.
6. Emite combinaciones de teclas corregidas mediante `/dev/uinput` para la distribución de salida seleccionada.
7. Vuelve a detectar el escáner si se desconecta y se conecta de nuevo.

No cambia tu distribución normal de teclado.

## Compilación

Requisitos:

- Linux
- Compilador de C
- Cabeceras del kernel que expongan `linux/input.h` y `linux/uinput.h`
- Acceso a `/dev/input/eventX`
- Acceso a `/dev/uinput`

Compilar:

```bash
make
```

Limpiar:

```bash
make clean
```

## Instalación como servicio

Instalar archivos:

```bash
sudo make install
```

Recargar udev y systemd:

```bash
sudo modprobe uinput
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl daemon-reload
```

Iniciar al arrancar:

```bash
sudo systemctl enable --now escanner.service
```

Ver logs:

```bash
journalctl -u escanner.service -f
```

La configuración de ejecución predeterminada se instala en:

```text
/etc/default/escanner
```

El instalador también añade:

```text
/etc/modules-load.d/escanner.conf
```

para que el módulo del kernel `uinput` se cargue automáticamente al arrancar.

Para una distribución de escritorio española:

```text
ESCANNER_OUTPUT_LAYOUT=es
```

Para otra distribución de escritorio, establece uno de estos valores:

```text
ESCANNER_OUTPUT_LAYOUT=us
ESCANNER_OUTPUT_LAYOUT=fr
ESCANNER_OUTPUT_LAYOUT=de
```

Después reinicia:

```bash
sudo systemctl restart escanner.service
```

El servicio ejecuta `scanner-bridge` continuamente. Si el escáner se desconecta, reintenta la detección y usa la nueva ruta `/dev/input/eventX` cuando vuelve a aparecer el mismo dispositivo `34eb:1502`.

## Uso manual sin sudo

Las reglas udev instaladas conceden acceso al nodo de evento del escáner probado y a `/dev/uinput` mediante `TAG+="uaccess"` para sesiones de escritorio activas, y mediante el grupo `input`.

Si tu distribución no aplica `uaccess` a tu sesión, añade tu usuario a `input` y cierra sesión/vuelve a entrar:

```bash
sudo usermod -aG input "$USER"
```

Nota de seguridad: pertenecer al grupo `input` puede permitir leer dispositivos de entrada. Para uso normal, se recomienda el servicio systemd.

## Herramientas

### `scanner-dump`

Herramienta de depuración. Lee el escáner e imprime eventos de tecla sin procesar junto con el texto reconstruido.

```bash
sudo build/scanner-dump
```

Después escanea:

```text
http://www.example.com
```

Salida esperada:

```text
SCAN[enter]: "http://www.example.com"
```

Usa `--grab` si quieres impedir que el texto original incorrecto llegue a la aplicación enfocada durante la depuración:

```bash
sudo build/scanner-dump --grab
```

### `scanner-bridge`

Puente de ejecución. Captura el escáner y emite texto corregido.

```bash
sudo build/scanner-bridge --output-layout es
```

Déjalo en ejecución, enfoca un editor de texto o un campo de entrada y escanea una URL.

Si tu escritorio o aplicación pierde caracteres, aumenta los retardos de emisión:

```bash
sudo build/scanner-bridge --key-delay-us 2000 --char-delay-us 6000
```

Para una salida más conservadora:

```bash
sudo build/scanner-bridge --key-delay-us 5000 --char-delay-us 15000
```

El modo de prueba captura y decodifica el escáner, pero no emite mediante `/dev/uinput`:

```bash
sudo build/scanner-bridge --dry-run
```

Resultado esperado:

```text
DRY-SCAN: "http://www.example.com"
```

## Filtro de seguridad de URL

Por defecto, `scanner-bridge` solo emite escaneos que parecen URL completas:

```text
http://...
https://...
```

Si recibe un fragmento como:

```text
https:/frag
```

registra:

```text
DROP: escaneo incompleto o URL invalida; no se emite.
```

Solo para depuración, permite fragmentos no válidos con:

```bash
sudo build/scanner-bridge --emit-invalid
```

## Limitaciones actuales

- Solo se autodetecta el dispositivo probado `34eb:1502 WCM HIDKeyBoard`.
- `es` es el único perfil de salida validado hasta ahora en la máquina real.
- `us`, `fr` y `de` están implementados a partir de distribuciones XKB básicas, pero necesitan validación en escritorios reales.
- Es un puente en espacio de usuario, no un controlador del kernel.
- La ejecución manual sin `sudo` depende de permisos udev/logind o de pertenecer al grupo `input`.

## Hoja de ruta

Ver [todo.md](todo.md).

Trabajo pendiente principal:

- matriz más amplia de pruebas de caracteres de URL
- validación real de los perfiles de salida `us`, `fr` y `de`

## Notas de desarrollo

Ver [lessons.md](lessons.md) para el diagnóstico del hardware y las lecciones aprendidas de la implementación.

## Registro de cambios

Ver [CHANGELOG.md](CHANGELOG.md).

## Licencia

MIT. Ver [LICENSE](LICENSE).
