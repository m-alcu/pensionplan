# pensionplan

Un simulador de jubilación para el sistema español, escrito en C++17 con SDL3 y
Dear ImGui (vendorizado en `src/vendor/imgui`, backends SDL3 + SDL_Renderer).

Parte de un **informe de simulación del INSS** —el que cualquiera puede
descargar de la Seguridad Social— y permite **volver a jugarlo con otro
futuro**: cambiar los periodos de cotización que quedan por delante, mover la
fecha de jubilación, simular un convenio especial y ver al instante la pensión
resultante, teniendo en cuenta la pensión máxima vigente ese año.

El informe oficial contesta a una sola pregunta: *si sigo como hasta ahora,
¿cuánto cobraré?* Este programa contesta a las otras: ¿y si dejo de trabajar
antes? ¿y si cotizo menos? ¿y si me jubilo más tarde? ¿cuánto de lo que cotizo
ahora mismo está comprando pensión de verdad?

## Por qué suele importar el tope

Para una carrera larga cotizada por bases altas, el resultado no lo decide la
base reguladora sino el **tope de las pensiones públicas**. Cuando el cálculo
inicial supera ese límite, la pensión se recorta hasta él, y entonces:

- subir las bases de cotización futuras **no cambia nada** — la pestaña
  *Proyecciones* lo enseña como una línea plana;
- lo único que mueve la cifra es **retrasar la jubilación** (un 4 % por año
  completo, que al estar ya en el tope se cobra como complemento aparte) o que
  cambie el propio tope;
- y la **tijera** se abre con los años: la base máxima de cotización sube por
  ley el IPC + 1,2 puntos al año hasta 2050, mientras el tope de la pensión sólo
  sube el IPC + 0,115. Se cotiza cada vez más por una pensión que, en
  proporción, crece cada vez menos.

De ahí sale la pregunta práctica que resuelve la pestaña *Convenio especial*:
**cuándo se puede dejar de trabajar sin perder pensión, y por qué base habría
que cotizar mientras tanto.**

## Configuración y datos

Nada del pensionista vive dentro del binario. Al arrancar, el programa lee:

```
config.ini              ; donde esta el fichero de datos
└─ data/pensionista.dat ; el pensionista entero
```

**`config.ini`** sólo indica la ruta:

```ini
[rutas]
root = data
fichero = pensionista.dat
```

Se busca, por este orden: la ruta de `PENSION_INI` si está definida, el
directorio de trabajo, el del ejecutable, y el inmediatamente superior —que es
donde queda cuando el binario está en `build/`—. Así el programa encuentra sus
datos tanto si se lanza desde el árbol de fuentes como desde un acceso directo.
Un `root` relativo se resuelve **respecto al directorio donde esté
`config.ini`**, no respecto al directorio de trabajo. Al arrancar, el programa
escribe por la salida de error qué `config.ini` ha usado y qué fichero de datos
ha abierto; si algo no cuadra, ahí se ve.

**El fichero `.dat`** es texto plano, por secciones, editable a mano:

| Sección | Qué guarda |
|---|---|
| `[titular]` | nombre, DNI, régimen, modalidad, fecha de nacimiento, fecha y versión del informe |
| `[informe]` | las cifras que imprimió la simulación oficial, para comparar |
| `[parametros]` | fecha de jubilación, IPC, anclajes, parámetros del convenio y todos los interruptores |
| `[vida_laboral]` | cada periodo de alta: empresa, fechas, días, y si es estimado |
| `[escenario]` | los periodos futuros que se están simulando |
| `[bases_cotizacion]` | las bases mensuales sobre las que hay registro |
| `[indice_precios]` | la serie mensual de IPC, normalizada |

Fechas en `dd/mm/aaaa`, meses en `aaaa-mm`, decimales con punto.

Si el fichero no existe, el programa arranca con el ejemplo ficticio de
[`examples/pensionista-ejemplo.dat`](examples/pensionista-ejemplo.dat) y lo dice
en la cabecera. El botón **Guardar datos** vuelca el escenario y los parámetros
actuales.

### Generar el fichero desde su propio informe

Descargue su simulación de jubilación del INSS y conviértala:

```sh
pdftotext -layout "Simulacion de Jubilacion.pdf" informe.txt
python3 tools/informe_a_dat.py informe.txt > data/pensionista.dat
```

La herramienta saca del PDF el titular, la vida laboral, las bases mes a mes, el
IPC —despejado de la columna IA, que es `IPC(ref)/IPC(mes)`— y las cifras del
resultado. Acepta **varios informes** del mismo titular:

```sh
python3 tools/informe_a_dat.py informe1.txt informe2.txt > data/pensionista.dat
```

Eso merece la pena porque cada informe sólo imprime las bases del periodo que
acaba usando. Si uno de ellos se resuelve por el cómputo dual lista bastantes
más meses, y juntándolos queda el historial completo sin nada que estimar.

> Los informes del INSS llevan nombre y DNI, y el `.dat` contiene el historial
> de cotización entero. `assets/`, `config.ini`, `data/` y cualquier `.dat`
> fuera de `examples/` están en `.gitignore` por ese motivo.

## Qué se puede simular

Panel izquierdo, siempre visible, con la pensión recalculándose en cada cambio:

- **Fecha de jubilación**, con la edad ordinaria que corresponda, los meses de
  anticipación o demora y el coeficiente aplicable.
- **Periodos futuros**: añadir, partir y eliminar tramos, cada uno cotizando, en
  desempleo, sin cotizar o en convenio especial, con su base inicial y su
  crecimiento anual, o siguiendo la base máxima legal.
- **Parámetros**: IPC proyectado, los puntos extra anuales del tope y de la base
  máxima, los anclajes, la base mínima para integrar lagunas y el porcentaje con
  el que estimar los meses anteriores al primero del que hay registro.
- **Presets**: informe oficial, base máxima real, dejar de cotizar ya, demorar
  dos años.

Siete pestañas: *Resumen* (el cálculo paso a paso, escenario contra informe),
*Informe* (todo lo que dice el PDF), *Vida laboral*, *Bases de cotización* (la
rejilla BI/IA/BA como la imprime el INSS, con el origen de cada mes), *Base
reguladora* (opción A contra opción B y el calendario de la DT 40.ª),
*Convenio especial* y *Proyecciones* (las gráficas).

## Lo que el motor implementa

Todo con la redacción del Real Decreto-ley 2/2023:

- **Base reguladora** (art. 209 LGSS): los 24 meses anteriores al mes previo al
  hecho causante entran a valor nominal y el resto se actualizan por IPC hasta
  el mes anterior a ese tramo.
- **Cómputo dual** (DT 40.ª): opción A, últimos 300 meses entre 350; opción B,
  las N bases más altas de una ventana más larga entre su divisor, con la tabla
  año a año desde 2026 (304/302/352,33) hasta su régimen definitivo en 2037
  (348/324/378). Se aplica la más favorable, y la B gana justo cuando la carrera
  tiene tramos bajos.
- **Integración de lagunas** (art. 209.1.b): los meses sin obligación de cotizar
  dentro del periodo de cómputo se rellenan con la base mínima, los 48 más
  recientes al 100 % y el resto al 50 %.
- **Porcentaje por años cotizados** (art. 210.1, escala final de la Ley
  27/2011): 50 % a los 15 años, +0,19 % al mes durante 248 meses y +0,18 %
  durante 16 más, llegando al 100 % a los 37 años.
- **Edad ordinaria**: 67 años, o 65 con 38 años y 6 meses cotizados. Depende de
  lo cotizado *en esa misma fecha*, y el programa lo resuelve. Es una trampa
  real: adelantar la jubilación recorta la carrera, lo que puede empujar la edad
  ordinaria de 65 a 67 y dejar el adelanto por encima de los 24 meses legales.
- **Jubilación anticipada** (art. 208): tabla completa de coeficientes
  reductores por meses de adelanto y tramo de carrera, aplicados también sobre
  el tope máximo (art. 210.3, con la transición terminada en 2033), y los
  requisitos de 35 años cotizados y de alta o situación asimilada.
- **Jubilación demorada** (art. 210.2): 4 % por año completo, más 2 % por un
  resto de más de seis meses a partir del segundo año.
- **Convenio especial** (Orden TAS/2865/2003): días computables como alta, cuota
  `base × 28,30 % × 0,94 + base × MEI`, prorrateo sobre 30 días de los meses
  partidos (art. 7.2.c), un buscador por bisección de la base mínima que
  mantiene la pensión intacta para cada fecha de cese, y la fecha más temprana a
  la que se puede cancelar sin perder nada.
- **Tope máximo y base máxima**: proyectados desde los anclajes del año base con
  sus respectivos incrementos adicionales hasta 2050.

## Cómo se comprueba

`tests/pension_tests.cpp` ejecuta tres niveles:

1. **Las tablas legales por sí solas**: calendario, calendario de la DT 40.ª,
   coeficientes reductores, MEI, proyecciones del tope y de la base máxima, y la
   fórmula de la cuota del convenio —contrastada contra el ejemplo que publica
   la propia guía de la Seguridad Social, 2.000 € → 550,04 €/mes.
2. **El ejemplo ficticio** de `examples/`: carrera completa, ventanas de
   cómputo, tope que muerde, anticipación, demora, convenio especial con su
   cancelación anticipada, y la ida y vuelta por el fichero de datos.
3. **Un informe real, si lo hay**. La sección `[informe]` del `.dat` guarda lo
   que imprimió el INSS, así que el motor tiene que reproducirlo:

   ```sh
   PENSION_FIXTURE=data/pensionista.dat ctest --test-dir build --output-on-failure
   ```

   Sobre el informe con el que se desarrolló el programa, lo reproduce al
   céntimo: días cotizados, base reguladora, porcentaje, tope, exceso, pensión y
   valor deflactado. Las diferencias de uno o dos céntimos vienen de que el
   informe redondea cada base actualizada antes de sumarlas.

Varias constantes se dedujeron resolviendo hacia atrás contra el informe y
cuadran exactamente, lo que confirma la lectura: el tope de un año futuro sale
de aplicar `(1 + IPC + 0,115 %)` al tope vigente; el valor deflactado, de
dividir por `(1 + IPC)`; y la serie de IPC se despeja de la columna IA, que es
`IPC(ref)/IPC(mes)` con la referencia fijada por la fecha de jubilación.

## Lo que es estimación, y está marcado como tal

El programa avisa en pantalla de cada uno de estos puntos:

- **El complemento por demora cuando la pensión ya está topada.** El art. 210.2
  LGSS prevé que la parte del incentivo que no cabe en la mensualidad se cobre
  aparte, pero la fórmula exacta y la alternativa a tanto alzado no están
  modeladas al detalle: se calcula como el importe desplazado por el tope,
  limitado a la base máxima de cotización.
- **Los meses anteriores al primero del que hay registro.** Si se adelanta mucho
  la jubilación, el periodo de cómputo puede llegar más atrás de lo que detalla
  el informe. Esos meses se estiman como un porcentaje (100 % por defecto) de la
  base máxima real de cada año.
- **La jubilación anticipada involuntaria** se modela como el 75 % del
  coeficiente voluntario: una regla aproximada, no una tabla legal.
- **El suelo del convenio especial.** Se usa la base mínima del Régimen General,
  que es la que ofrece el simulador oficial. La letra c) del art. 6.2.1 remite
  hoy a la tabla general del RETA, más baja. El campo es editable.
- **Cómo evoluciona la base del convenio.** Crece a un porcentaje anual
  editable, 2 % por defecto, que es lo que hace el simulador oficial. La norma
  lo plantea distinto: el art. 6.2.2 la ata a la subida de la base mínima y el
  6.2.3 permite indexarla a la máxima.
- **El convenio especial por despido colectivo.** Si la salida fuese por esa
  vía, el art. 20 de la orden y la DA 13.ª LGSS obligan a la empresa a pagar el
  convenio de los trabajadores de 55 o más años hasta que cumplan 63 —o 61 por
  causas económicas—. No está modelado, porque no es una palanca que el
  interesado pueda accionar, pero cambia la aritmética si llega a darse.

Y, como dice el propio informe del INSS: esto es un cálculo informativo, no
genera derechos ni expectativas.

## Compilar y ejecutar

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/pensionplan
```

Los tests del motor (sin SDL ni ImGui):

```sh
ctest --test-dir build --output-on-failure
```

Variables de entorno para pruebas sin interacción: `PENSION_SHOT` (guarda un BMP
y sale), `PENSION_TAB` (0-6), `PENSION_RETIRE_YEAR`, `PENSION_INI` (ruta
alternativa de `config.ini`) y `PENSION_FIXTURE` (informe real contra el que
contrastar).

## Fuentes de los datos legales

- Ley General de la Seguridad Social, arts. 57, 205, 207, 208, 209, 210 y
  disposiciones transitorias 9.ª y 40.ª, en la redacción dada por el
  [Real Decreto-ley 2/2023](https://www.boe.es/buscar/act.php?id=BOE-A-2015-11724).
- Calendario del cómputo dual 2026-2037:
  [WTW](https://www.wtwco.com/es-es/insights/2026/03/como-se-calcula-mi-base-reguladora-en-la-pension-de-jubilacion),
  [Iberley (DT 40.ª)](https://www.iberley.es/legislacion/dt-40-tr-ley-general-seguridad-social-lgss),
  verificado además contra los propios informes.
- Tabla de coeficientes reductores por anticipación:
  [VidaCaixa](https://www.vidacaixa.es/en/articulos/jubilacion-y-pensiones/coeficientes-reductores-jubilacion-anticipada),
  contrastada con [Jubilistos](https://jubilistos.com/coeficientes-reductores-explicados/).
- Pensión máxima 2026 (3.359,60 €/mes en 14 pagas):
  [Zurich](https://www.zurich.es/blog/pension-maxima-minima-espana-2026).
- Convenio especial: el
  [texto consolidado de la Orden TAS/2865/2003](https://www.boe.es/buscar/act.php?id=BOE-A-2003-19281)
  (arts. 6, 7, 14 y 20) y la
  [guía oficial de la Seguridad Social](https://portal.seg-social.gob.es/wps/portal/importass/importass/Colectivos/convenios+especiales/guia).
- Bases máximas de cotización históricas del Régimen General, contrastadas
  contra las que imprimen los informes.
