# Lessons learned

## Diagnostico confirmado

- El escaner no se identifica como un modelo comercial claro.
- `lsusb` muestra:

```text
ID 34eb:1502 WCM HIDKeyBoard
```

- El descriptor USB confirma que es HID teclado:

```text
bInterfaceClass    3 Human Interface Device
bInterfaceSubClass 1 Boot Interface Subclass
bInterfaceProtocol 1 Keyboard
```

- El dispositivo de input observado fue:

```text
/dev/input/event6
```

- `udev` lo ve como:

```text
ID_VENDOR_ID=34eb
ID_MODEL_ID=1502
ID_SERIAL=WCM_HIDKeyBoard_00000000011C
ID_INPUT_KEYBOARD=1
```

## Sintoma

Al escanear:

```text
http://www.example.com
```

aparece:

```text
httpÑ--www.example.com
```

Esto indica una discrepancia de layout:

```text
: -> Ñ
/ -> -
```

## Intentos ya realizados

Se creo una regla `hwdb` para fijar layout US solo al lector:

```text
evdev:input:b0003v34EBp1502*
 XKB_FIXED_LAYOUT=us
 XKB_FIXED_VARIANT=
```

Tambien se probo forzar `XKBLAYOUT=us` mediante regla udev.

La comprobacion llego a mostrar:

```text
XKB_FIXED_LAYOUT=us
XKBLAYOUT=us
```

pero el escaneo seguia saliendo como:

```text
httpÑ--www.example.com
```

Conclusion: el entorno grafico/aplicacion sigue interpretando el dispositivo con el layout efectivo global o ignora el layout por dispositivo para este flujo.

## Restriccion importante

No se debe cambiar el layout global del sistema. El teclado principal del usuario es espanol y debe seguir siendo espanol.

## Manual del escaner

El manual disponible solo ofrece:

- `UNITED STATES`
- `FRANCE`

No ofrece `SPANISH`.

Por tanto, configurar el escaner como teclado espanol no parece una opcion con el manual actual.

## Decision de arquitectura

La solucion correcta no debe intentar seguir peleando con XKB global.

Debe capturar el dispositivo `34eb:1502` en crudo, traducir los keycodes US HID a texto logico y reemitir el texto corregido por un canal controlado.

Orden recomendado:

1. `evdev` para leer keycodes del lector.
2. `EVIOCGRAB` para bloquear la salida rota original.
3. Tabla de traduccion US HID -> texto.
4. `uinput` o backend equivalente para reemitir texto corregido.

## Riesgos tecnicos

- `uinput` emite teclas, no texto abstracto. Si se emiten keycodes US bajo layout espanol, se reproducira el problema.
- Para `uinput`, la salida debe estar adaptada al layout efectivo del sistema o usar una estrategia que emita texto por otro canal.
- Wayland puede limitar inyeccion de texto desde herramientas graficas.
- Un modulo kernel seria mas invasivo y no deberia ser la primera opcion.
- `/dev/input/event6` no es legible por el usuario normal en este sistema. La captura necesita `sudo` o una regla udev de permisos.

## Implementacion iniciada

- `build/scanner-dump` compila correctamente.
- Dentro del sandbox, la autodeteccion encuentra `/dev/input/event6`, pero no puede abrir el nodo real.
- Fuera del sandbox, el nodo real existe, pero abrirlo sin `sudo` da `Permission denied`.
- `sudo -n` no sirve porque requiere contrasena.
- La primera captura real demostro que los keycodes de letras Linux no son contiguos alfabeticamente. La conversion `KEY_A + offset` era incorrecta; debe usarse una tabla explicita.
- `scanner-bridge --dry-run` funciona: aplica `EVIOCGRAB`, bloquea la salida rota en el editor y reconstruye `DRY-SCAN: "http://www.example.com"`.
- El escaner termina el escaneo con `KEY_ENTER`.
- `scanner-bridge` en modo real funciona para `http://www.example.com`: el editor recibe `http://www.example.com` y el terminal muestra `SCAN: "http://www.example.com"`.
- La estrategia `evdev` + `EVIOCGRAB` + `uinput` es viable para este lector sin cambiar el layout global del sistema.
- Se observo un escaneo puntual incompleto (`https:/frag`). El bridge debe descartar por defecto escaneos que no parezcan URL completa para evitar escribir basura en el editor.
- En URLs largas, el terminal mostro todos los `SCAN` correctos pero el editor recibio fragmentos intercalados. La captura esta bien; el problema es la velocidad de emision por `uinput`. Se anadieron retardos configurables `--key-delay-us` y `--char-delay-us`.
- La emision bloqueante tambien podia hacer que escaneos muy rapidos se acumulasen en el buffer de entrada. Se separo lectura y emision con una cola y un hilo emisor.
- Los retardos que resultan comodos para el usuario son `--key-delay-us 1000 --char-delay-us 3000`; quedan como valores por defecto.
- La reconexion debe redetectar por `34eb:1502`, no reutilizar ciegamente `/dev/input/event6`, porque el kernel puede asignar otro `eventX` despues de desconectar y reconectar.
- El servicio systemd puede ejecutarse como servicio de sistema y evita dejar una terminal abierta. Para uso manual sin `sudo`, hacen falta permisos udev sobre el `eventX` del lector y `/dev/uinput`.
- `uaccess` es preferible para sesiones de escritorio activas; el grupo `input` es una alternativa practica, pero tiene implicaciones de seguridad porque puede leer dispositivos de entrada.
- El soporte de idiomas no significa cambiar el firmware del lector. El lector se sigue interpretando como entrada US HID y lo que cambia es el perfil de salida del teclado virtual: `--output-layout es`, `us`, `fr` o `de`.
- El perfil `es` esta validado en la maquina real. `us`, `fr` y `de` estan implementados desde mapas XKB basicos y deben validarse en escritorios reales antes de considerarlos igual de probados.

## Criterio de exito

Con el driver activo:

```text
http://www.example.com
```

debe llegar exactamente asi a cualquier campo de texto razonable, mientras el teclado Logitech sigue usando espanol.
