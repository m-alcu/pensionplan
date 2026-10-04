#!/usr/bin/env python3
"""Convierte uno o varios informes de simulación del INSS en un fichero .dat.

    pdftotext -layout "Simulacion de Jubilacion.pdf" informe.txt
    python3 tools/informe_a_dat.py informe.txt > data/pensionista.dat

Admite varios informes del mismo titular. Eso es útil porque cada informe sólo
imprime las bases del periodo que acaba usando: si uno de ellos se resuelve por
el cómputo dual, lista bastantes más meses, y juntándolos queda el historial
completo. Las cabeceras y las cifras del resultado se toman del primero.

Nada de este fichero es específico de una persona: todo sale del PDF.
"""
import re
import sys

# El simulador del INSS proyecta la inflación al 2 % a partir del último mes con
# IPC publicado. Esa cola se detecta sola —sus saltos mensuales son exactamente
# 1,02^(1/12)— y se recorta, para que el programa proyecte con su propia
# hipótesis en lugar de heredar la del informe.
PROYECCION_MENSUAL = 1.02 ** (1 / 12)
MESES = ['ENE', 'FEB', 'MAR', 'ABR', 'MAY', 'JUN',
         'JUL', 'AGO', 'SEP', 'OCT', 'NOV', 'DIC']


def num(tok):
    """'4.055,96' -> 4055.96; '--' y vacíos -> None."""
    tok = tok.strip()
    if not tok or tok == '--':
        return None
    try:
        return float(tok.replace('.', '').replace(',', '.'))
    except ValueError:
        return None


def buscar(texto, patron, grupo=1, por_defecto=''):
    # re.M para que '$' valga como fin de linea y no de fichero.
    m = re.search(patron, texto, re.M)
    return m.group(grupo).strip() if m else por_defecto


def parse_bases(lineas):
    """La rejilla BASES DE COTIZACIÓN: {(año, mes): (bi, ia)}."""
    crudo, año = {}, None
    for cruda in lineas:
        ln = cruda.strip()
        m = re.match(r'^(20[0-9]\d)\s+ENE\s+FEB', ln)
        if m:
            año = int(m.group(1))
            crudo.setdefault(año, {})
            continue
        if año is None:
            continue
        m = re.match(r'^(BI|IA)\s+(.*)$', ln)
        if m:
            crudo[año][m.group(1)] = [num(t) for t in m.group(2).split()]

    # El primer y el último año salen incompletos. El primero pierde columnas
    # por la izquierda y el último por la derecha; una fila IA que no llegue a
    # doce hay que alinearla en consecuencia, o todo el año queda desplazado un
    # mes y la base reguladora sale mal por unos céntimos.
    datos = {}
    if not crudo:
        return datos
    primero, ultimo = min(crudo), max(crudo)
    for año, filas in crudo.items():
        for fila, valores in filas.items():
            if len(valores) < 12:
                relleno = [None] * (12 - len(valores))
                valores = relleno + valores if año == primero else valores + relleno
            for i, v in enumerate(valores[:12]):
                if v is None:
                    continue
                clave = (año, i + 1)
                bi, ia = datos.get(clave, (None, None))
                datos[clave] = (v, ia) if fila == 'BI' else (bi, v)
    return datos


def parse_periodos(texto):
    """La tabla PERIODOS LABORALES."""
    bloque = re.search(r'PERIODOS LABORALES(.*?)Vida Laboral', texto, re.S)
    if not bloque:
        return []
    filas = []
    for ln in bloque.group(1).split('\n'):
        m = re.match(r'^\s*(.+?)\s{2,}(\d{2}/\d{2}/\d{4})\s+(\d{2}/\d{2}/\d{4})\s+([\d.]+)\s*$',
                     ln)
        if not m:
            continue
        desc = re.sub(r'\s+', ' ', m.group(1)).strip()
        empresa, _, detalle = desc.partition(' / ')
        filas.append((empresa.strip(), detalle.strip() or 'General',
                      m.group(2), m.group(3), int(m.group(4).replace('.', ''))))
    return filas


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    textos = [open(p, encoding='utf-8').read() for p in sys.argv[1:]]
    principal = textos[0]

    # --- cabecera ---------------------------------------------------------
    nombre = buscar(principal, r'Titular:\s*(.+?)\s{2,}')
    dni = buscar(principal, r'DNI/NIE:\s*(\S+)')
    nacimiento = buscar(principal, r'F\. Nacimiento:\s*(\d{2}/\d{2}/\d{4})')
    jubilacion = buscar(principal, r'Fecha jubilaci[oó]n:\s*(\d{2}/\d{2}/\d{4})')
    regimen = buscar(principal, r'R[eé]gimen que resuelve:\s*(.+?)\s*$', 1, 'General')
    modalidad = buscar(principal, r'Modalidad de jubilaci[oó]n:\s*(.+?)\s*$')
    calculo = buscar(principal, r'Fecha del c[aá]lculo\s*:\s*(\d{2}/\d{2}/\d{4})')
    version = buscar(principal, r'Fecha del c[aá]lculo\s*:\s*\d{2}/\d{2}/\d{4}\s+(\S+)')

    def cifra(patron, texto=principal):
        return num(buscar(texto, patron)) or 0.0

    informe = {
        'dias_vida_laboral': int(cifra(r'Durante toda la vida laboral\s+([\d.]+)\s*d[ií]as')),
        'dias_ultimos_15_anos': int(cifra(r'carencia espec[ií]fica\)\s+([\d.]+)\s*d[ií]as')),
        'anos_computables': cifra(r'[Aa]ños computables a efectos de porcentaje\s+([\d.,]+)\s*años'),
        'base_reguladora': cifra(r'Base Reguladora\s+([\d.,]+)\s*€'),
        'suma_bases': cifra(r'=\s*([\d.,]+)\s*/\s*3[0-9.,]+\s*=') or
                      cifra(r'\(25 años\)=([\d.,]+)/'),
        'porcentaje_total': cifra(r'Porcentaje total[^\n]*?([\d.,]+)\s*%'),
        'calculo_inicial': cifra(r'C[aá]lculo inicial[^\n]*?([\d.,]+)\s*€'),
        'pension_maxima': cifra(r'L[ií]mite pensi[oó]n m[aá]xima\s+([\d.,]+)\s*€'),
        'exceso': cifra(r'Exceso l[ií]mite pensi[oó]n\s+([\d.,]+)\s*€'),
        'pension': cifra(r'Importe de la pensi[oó]n:\s*([\d.,]+)\s*€'),
        'pension_deflactada': cifra(r'equivaldr[aá] a\s*([\d.,]+)\s*euros'),
    }
    superpuestos = int(cifra(r'D[ií]as superpuestos\s+(\d+)'))

    # --- bases e IPC, fundiendo todos los informes -------------------------
    bases, ia = {}, {}
    for texto in textos:
        for clave, (bi, indice) in parse_bases(texto.split('\n')).items():
            if bi is not None:
                bases.setdefault(clave, bi)
            if indice is not None:
                ia.setdefault(clave, indice)

    periodos = parse_periodos(principal)
    if not periodos:
        sys.exit('No se ha podido leer PERIODOS LABORALES del informe.')
    # La última fila es el tramo que el propio informe estima; ahí empieza el
    # escenario, y ahí termina el historial registrado.
    *registrados, estimado = periodos
    corte_dia, corte_mes, corte_año = (int(x) for x in estimado[2].split('/'))
    traspaso = (corte_año, corte_mes)

    # Sólo son historial los meses anteriores al traspaso; lo de después es la
    # proyección del informe, que el escenario sustituye.
    historial = {k: v for k, v in bases.items() if k < traspaso}

    # IA(m) = IPC(ref)/IPC(m), así que 1/IA es el IPC salvo escala. Se conservan
    # los meses de IPC realmente publicado, que son los anteriores al traspaso.
    reales = {k: v for k, v in ia.items() if k < traspaso and v}
    if not reales:
        sys.exit('No se ha podido leer la columna IA.')
    meses = sorted(reales)
    while len(meses) > 2:
        salto = (1.0 / reales[meses[-1]]) / (1.0 / reales[meses[-2]])
        if abs(salto - PROYECCION_MENSUAL) > 1e-5:
            break
        meses.pop()
    escala = reales[meses[-1]]
    ipc = {k: (1.0 / reales[k]) * escala for k in meses}

    base_escenario = bases.get(traspaso) or max(historial.values())

    # --- salida -----------------------------------------------------------
    w = sys.stdout.write
    w('; Datos del pensionista, extraidos de la simulacion oficial del INSS.\n')
    w('; Generado por tools/informe_a_dat.py; se puede editar a mano.\n')
    w('; Fechas dd/mm/aaaa, meses aaaa-mm, decimales con punto.\n')
    w('\n[formato]\nversion = 1\n')

    w('\n[titular]\n')
    for k, v in (('nombre', nombre), ('dni', dni), ('regimen', regimen),
                 ('modalidad', modalidad), ('fecha_calculo', calculo),
                 ('version_informe', version), ('nacimiento', nacimiento)):
        w('%s = %s\n' % (k, v))

    w('\n[informe]\n; Lo que imprimio la simulacion oficial, para comparar.\n')
    for k, v in informe.items():
        w('%s = %s\n' % (k, ('%d' % v) if k.startswith('dias') else ('%.4f' % v)))

    w('\n[parametros]\n')
    for k, v in (('jubilacion', jubilacion), ('ipc', '2.000'), ('tope_extra_pp', '0.115'),
                 ('base_maxima_extra_pp', '1.200'), ('ano_ancla', '2026'),
                 ('tope_ancla', '3359.60'), ('base_maxima_ancla', '5101.20'),
                 ('base_minima_ancla', '1424.40'), ('convenio_tipo_pct', '28.300'),
                 ('convenio_coeficiente', '0.940'), ('convenio_base_minima', '1424.40'),
                 ('base_estimada_pct', '100.00'), ('anticipada_voluntaria', 'si'),
                 ('computo_dual', 'si'), ('integrar_lagunas', 'si'),
                 ('recortar_base_maxima', 'si'), ('cancelar_convenio_antes', 'si')):
        w('%s = %s\n' % (k, v))

    w('\n[vida_laboral]\n; empresa | detalle | alta | baja | dias | estimado\n')
    w('dias_superpuestos = %d\n' % superpuestos)
    w('traspaso = %04d-%02d\n' % traspaso)
    for empresa, detalle, alta, baja, dias in registrados:
        w('periodo = %s | %s | %s | %s | %d | no\n' % (empresa, detalle, alta, baja, dias))
    w('periodo = %s | %s | %s | %s | %d | si\n' % estimado)
    w('\n[escenario]\n')
    w('; desde | hasta | situacion | base | subida_anual | seguir_base_maxima | etiqueta\n')
    w('periodo = %s | %s | cotizando | %.2f | 2.000 | no | %s\n'
      % (estimado[2], estimado[3], base_escenario, estimado[0]))

    w('\n[bases_cotizacion]\n; Bases de cotizacion sobre las que hay registro, una por mes.\n')
    for (año, mes) in sorted(historial):
        w('%04d-%02d = %.2f\n' % (año, mes, historial[(año, mes)]))

    w('\n[indice_precios]\n; IPC mensual publicado, normalizado al ultimo mes de la serie.\n')
    for (año, mes) in sorted(ipc):
        w('%04d-%02d = %.8f\n' % (año, mes, ipc[(año, mes)]))


if __name__ == '__main__':
    main()
