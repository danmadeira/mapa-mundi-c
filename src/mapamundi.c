/**
 * @file mapamundi.c
 * @author Daniel Madeira
 * @brief Gerador de mapa-múndi em SVG com suporte a múltiplas projeções cartográficas.
 * @version 1.0
 * @date 2026-09-23
 *
 * @details Este programa lê dados geográficos de continentes (via GeoJSON)
 * e cidades (via CSV) para gerar um mapa vetorial (SVG) da superfície da Terra.
 * Suporta dezenas de projeções matemáticas (ex: Mercator, Robinson, Mollweide,
 * Equal Earth, entre outras) e permite a personalização da exibição de linhas de
 * grade (graticule) e marcadores de cidades através de argumentos de linha de comando.
 *
 * @note
 * \b Instruções \b de \b Compilação:
 * Certifique-se de ter as bibliotecas `cJSON` e `libcsv` instaladas no seu sistema.
 * Utilize o seguinte comando no GCC para compilar, garantindo o link das bibliotecas
 * matemáticas e de parsing:
 * @code
 * gcc -Wall -O3 mapamundi.c -o mapamundi -lm -lcjson -lcsv
 * @endcode
 *
 * @copyright Copyright (c) 2026 Daniel Madeira. Todos os direitos reservados.
 * @license https://opensource.org/license/GPL-3.0 GNU Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <cjson/cJSON.h>
#include <csv.h>

#define PI 3.14159265359
#define MAX_PATH 1024
#define MAX_LINE 4096

typedef struct {
    double x;
    double y;
} CoordenadaXY;

typedef struct {
    FILE *svg;
    int coluna_atual;
    int is_header;
    double lat;
    double lon;
    int raio;
} ContextoCSV;

char csv_path[MAX_PATH] = "";
char geojson_path[MAX_PATH] = "";
int img_largura = 800;
int img_altura = 400;
char projecao_cod = 'c';
char output_file[MAX_PATH] = "";

/**
 * @brief Converte a latitude geográfica em latitude autálica.
 *
 * Utilizada em projeções de área igual (como Equal Earth) para compensar
 * o achatamento dos polos da Terra, convertendo a latitude baseada num
 * elipsoide para a latitude de uma esfera com a mesma área de superfície.
 *
 * @param latitude Coordenada geográfica de latitude em graus.
 * @return O valor da latitude autálica em graus.
 */
double converter_geografica_para_autalica(double latitude) {
    if (fabs(latitude) >= 90) return latitude;
    double phi = latitude * PI / 180.0;
    double e = 0.0818191908426;
    double e2 = 0.00669437999014;
    double q = (1 - e2) * ((sin(phi) / (1 - e2 * pow(sin(phi), 2))) - (1 / (2 * e)) * log((1 - e * sin(phi)) / (1 + e * sin(phi))));
    double qp = (1 - e2) * ((1 / (1 - e2)) - (1 / (2 * e)) * log((1 - e) / (1 + e)));
    double latitude_autalica = asin(q / qp);
    return (latitude_autalica * 180.0 / PI);
}

/**
 * @brief Exibe as instruções de uso e ajuda do programa.
 *
 * Imprime no console os parâmetros aceites pela linha de comandos,
 * incluindo os parâmetros de dimensão, projeções disponíveis e opções de desenho.
 *
 * @param prog_name Nome do executável (geralmente argv[0]).
 */
void exibir_ajuda(const char *prog_name) {
    printf("Mapa-múndi em SVG\n");
    printf("Uso: %s [-t LARGURAxALTURA] [-p PROJEÇÃO] [-o ARQUIVO_SAÍDA] [-c] [-l] [-h]\n\n", prog_name);
    printf("Opções:\n");
    printf("  -t  Dimensões da imagem em pixels (ex: 1200x800). Padrão: 800x400\n");
    printf("  -p  Nome ou letra identificadora da projeção. Padrão: PlateCarree\n");
    printf("      Opções: EckertIV (e), EckertVI (E), EqualEarth (i), GottEqualArea (g), GottAzimuthal (G),\n");
    printf("              Hammer (h), KavrayskiyVII (k), Lambert (l), Mercator (t), Miller (m),\n");
    printf("              Mollweide (M), NaturalEarth (n), NaturalEarthII (N), Patterson (p), PlateCarree (c),\n");
    printf("              Robinson (r), Sinusoidal (s), WagnerVI (w), WinkelTripel (W).\n");
    printf("  -o  Caminho e nome do arquivo SVG gerado. Padrão: <Projecao>.svg\n");
    printf("  -c  Exibe os pontos das cidades capitais\n");
    printf("  -l  Exibe as linhas de meridianos, paralelos e trópicos\n");
    printf("  -h  Exibe esta tela de ajuda\n\n");
    printf("Exemplo:\n");
    printf("  %s -t 1600x800 -p EqualEarth -o mapa.svg -lc\n", prog_name);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Eckert IV.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 256
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 221
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana resultante.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana resultante.
 */
void calcular_eckert_iv(double lat, double lon, double *x, double *y) {
    double theta = lat * (PI / 180.0);
    double k = (2.0 + (PI / 2.0)) * sin(theta);
    theta = theta / 2.0;
    for (int i = 0; i < 10; i++) {
        double sint = sin(theta);
        double cost = cos(theta);
        double v = (theta + sint * cost + 2.0 * sint - k) / (2.0 * cost * (1.0 + cost));
        theta -= v;
        if (fabs(v) < 1e-7) break;
    }
    double longitude = lon * (PI / 180.0);
    *x = (2.0 / sqrt(PI * (4.0 + PI))) * longitude * (1.0 + cos(theta));
    *y = 2.0 * sqrt(PI / (4.0 + PI)) * sin(theta);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Eckert VI.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 257
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 220
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_eckert_vi(double lat, double lon, double *x, double *y) {
    double theta = lat * (PI / 180.0);
    double k = (1.0 + (PI / 2.0)) * sin(theta);
    for (int i = 0; i < 10; i++) {
        double v = (theta + sin(theta) - k) / (1.0 + cos(theta));
        theta -= v;
        if (fabs(v) < 1e-7) break;
    }
    double longitude = lon * (PI / 180.0);
    *x = (longitude * (1.0 + cos(theta)) / sqrt(2.0 + PI));
    *y = (2.0 * theta / sqrt(2.0 + PI));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Gott equal-area elliptical.
 *        GOTT, J. R.; MUGNOLO, C.; COLLEY, W. N. Map Projections Minimizing Distance Errors. p 4
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_gott_equal_area(double lat, double lon, double *x, double *y) {
    double longitude = lon;
    if (longitude == 180.0) longitude -= 1.0;
    else if (longitude == -180.0) longitude += 1.0;
    longitude *= (PI / 180.0);
    double theta = lat * (PI / 180.0);
    double phi = asin(cos(theta) * sin(longitude / 2.0));
    double k = PI * sin(phi);
    double lambda = 0.5 * asin(sin(theta) / cos(phi));
    int fim = 1;
    for (int i = 0; i < 10; i++) {
        double v = (theta + sin(theta) - k) / (1.0 + cos(theta));
        theta -= v;
        if (fabs(v) < 1e-7) { fim = 0; break; }
    }
    if (fim) {
        if (theta < 0) theta = -PI / 2.0;
        else theta = PI / 2.0;
    } else {
        theta *= 0.5;
    }
    *x = sqrt(2.0) * sin(theta);
    *y = (PI / (2.0 * sqrt(2.0))) * lambda * cos(theta);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Gott-Mugnolo_azimuthal.
 *        GOTT, J. R.; MUGNOLO, C.; COLLEY, W. N. Map Projections Minimizing Distance Errors. p 8
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_gott_azimuthal(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double c = acos(cos(latitude) * cos(longitude));
    if (c < 1e-10) {
        *x = 0; *y = 0;
    } else {
        double k;
        if (fabs(sin(c)) < 1e-10) k = sin(0.446 * c) / 1e-10;
        else k = sin(0.446 * c) / sin(c);
        *x = k * cos(latitude) * sin(longitude);
        *y = k * sin(latitude);
    }
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Hammer.
 *        BUGAYEVSKIY, L. M.; SNYDER, J. P. *Map Projections A Reference Manual. p 176
 *        JENNY, B. Adaptive Composite Map Projections. p 3
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 232
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_hammer(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double eta = sqrt(1.0 + cos(latitude) * cos(longitude / 2.0));
    *x = (2.0 * sqrt(2.0) * cos(latitude) * sin(longitude / 2.0)) / eta;
    *y = (sqrt(2.0) * sin(latitude)) / eta;
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção Equal Earth.
 *        ŠAVRIČ, B.; PATTERSON, T.; JENNY, B. The Equal Earth map projection. p 456
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_equal_earth(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double sqrt3 = sqrt(3.0);
    double A1 = 1.340264, A2 = -0.081106, A3 = 0.000893, A4 = 0.003796;
    double theta = asin((sqrt3 / 2.0) * sin(latitude));
    double t2 = pow(theta, 2);
    double t6 = pow(theta, 6);
    *x = (2.0 * sqrt3 * longitude * cos(theta)) / (3.0 * (A1 + 3.0 * A2 * t2 + t6 * (7.0 * A3 + 9.0 * A4 * t2)));
    *y = theta * (A1 + A2 * t2 + t6 * (A3 + A4 * t2));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Kavrayskiy VII.
 *        SNYDER, J. P. Flattening the Earth: Two thousand years of map projections. p 202
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_kavrayskiy_vii(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    *x = (3.0 * longitude / 2.0) * sqrt(1.0 / 3.0 - pow(latitude / PI, 2));
    *y = latitude;
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Lambert azimuthal equal-area.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 185
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_lambert_azimuthal(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double denominador = 1.0 + cos(latitude) * cos(longitude);
    if (denominador < 1e-10) denominador = 1e-10;
    double k = sqrt(2.0 / denominador);
    *x = k * cos(latitude) * sin(longitude);
    *y = k * sin(latitude);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Miller cylindrical.
 *        WEISSTEIN, E. W. Miller Cylindrical Projection
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_miller(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    *x = lon * (PI / 180.0);
    *y = (5.0 / 4.0) * log(tan(PI / 4.0 + (2.0 * latitude) / 5.0));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Mollweide.
 *        WEISSTEIN, E. W. Mollweide Projection.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 251
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 220
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_mollweide(double lat, double lon, double *x, double *y) {
    double theta = lat * (PI / 180.0);
    double k = PI * sin(theta);
    int fim = 1;
    for (int i = 0; i < 10; i++) {
        double v = (theta + sin(theta) - k) / (1.0 + cos(theta));
        theta -= v;
        if (fabs(v) < 1e-7) { fim = 0; break; }
    }
    if (fim) {
        if (theta < 0) theta = -PI / 2.0;
        else theta = PI / 2.0;
    } else {
        theta *= 0.5;
    }
    *x = (2.0 * sqrt(2.0) / PI) * (lon * (PI / 180.0)) * cos(theta);
    *y = sqrt(2.0) * sin(theta);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção Natural Earth.
 *        ŠAVRIČ, B.; JENNY, B.; PATTERSON, T.; PETROVIČ, D.; HURNI, L. A Polynomial Equation for the Natural Earth Projection. p 366
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_natural_earth(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    *x = longitude * (0.870700 - 0.131979 * pow(latitude, 2) - 0.013791 * pow(latitude, 4) + 0.003971 * pow(latitude, 10) - 0.001529 * pow(latitude, 12));
    *y = 1.007226 * latitude + 0.015085 * pow(latitude, 3) - 0.044475 * pow(latitude, 7) + 0.028874 * pow(latitude, 9) - 0.005916 * pow(latitude, 11);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção Natural Earth II.
 *        ŠAVRIČ, B.; PATTERSON, T.; JENNY, B. The Natural Earth II world map projection. p 125
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_natural_earth_ii(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double lat2 = latitude * latitude;
    double lat4 = lat2 * lat2;
    double lat6 = lat2 * lat4;
    *x = longitude * (0.84719 - 0.13063 * lat2 + lat6 * lat6 * (-0.04515 + 0.05494 * lat2 - 0.02326 * lat4 + 0.00331 * lat6));
    *y = latitude * (1.01183 + lat4 * lat4 * (0.01926 * lat2 - 0.00396 * lat4 - 0.02625));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Patterson.
 *        PATTERSON, T.; ŠAVRIČ, B.; JENNY, B. Introducing the Patterson Cylindrical Projection. p 80
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_patterson(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double lat2 = latitude * latitude;
    *x = lon * (PI / 180.0);
    *y = latitude * (1.0148 + lat2 * lat2 * (0.23185 + lat2 * (-0.14499 + lat2 * 0.02406)));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Robinson
 *        IPBUKER, C. A computational approach to the Robinson projection. p 207
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_robinson(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    *x = (2.6666 - 0.367 * pow(latitude, 2) - 0.150 * pow(latitude, 4) + 0.0379 * pow(latitude, 6)) * longitude / PI;
    double s = (latitude >= 0) ? 1.0 : -1.0;
    *y = 0.96047 * latitude - 0.00857 * s * pow(fabs(latitude), 6.41);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção Sinusoidal.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 247
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 220
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_sinusoidal(double lat, double lon, double *x, double *y) {
    *x = (lon * (PI / 180.0)) * cos(lat * (PI / 180.0));
    *y = lat * (PI / 180.0);
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Mercator.
 *        SNYDER, J. P. Map Projections - A Working Manual. p 41
 *        PEARSON, F. Map Projections: Theory and Applications. pp 190-191
 *        GOLDBERG, D. M.; GOTT, J. R. Flexion and Skewness in Map Projections of the Earth. p 8
 *        IOGP Coordinate Conversions and Transformations including Formulas. p 45
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 218
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_mercator(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    *x = lon * (PI / 180.0);
    if (latitude > 1.4844222297453322) *y = PI;
    else if (latitude < -1.4844222297453322) *y = -PI;
    else *y = log(tan(PI/4.0 + latitude/2.0));
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Wagner VI.
 *        SNYDER, J. P. Flattening the Earth: Two thousand years of map projections. p 205
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_wagner_vi(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    *x = (lon * (PI / 180.0)) * sqrt(1.0 - 3.0 * pow(latitude / PI, 2));
    *y = latitude;
}

/**
 * @brief Calcula as coordenadas cartesianas X e Y usando a projeção de Winkel Tripel.
 *        SNYDER, J. P.; VOXLAND, P. M. An Album of Map Projections. p 233
 *
 * @param lat Coordenada geográfica de latitude em graus.
 * @param lon Coordenada geográfica de longitude em graus.
 * @param x   Ponteiro onde será armazenada a coordenada X cartesiana.
 * @param y   Ponteiro onde será armazenada a coordenada Y cartesiana.
 */
void calcular_winkel_iii(double lat, double lon, double *x, double *y) {
    double latitude = lat * (PI / 180.0);
    double longitude = lon * (PI / 180.0);
    double alpha = acos(cos(latitude) * cos(longitude / 2.0));
    double sinc = (alpha == 0) ? 1.0 : sin(alpha) / alpha;
    *x = (1.0 / 2.0) * (longitude * cos(acos(2.0 / PI)) + ((2.0 * cos(latitude) * sin(longitude / 2.0)) / sinc));
    *y = (1.0 / 2.0) * (latitude + (sin(latitude) / sinc));
}

/**
 * @brief Converte coordenadas geográficas num ponto de pixel (X, Y) para o ecrã/SVG.
 *
 * Com base na projeção selecionada, calcula as coordenadas cartesianas relativas
 * e aplica o fator de escala (módulo) e o deslocamento para centralizar o mapa
 * nas dimensões especificadas da imagem (largura e altura).
 *
 * @param latitude  Coordenada geográfica de latitude.
 * @param longitude Coordenada geográfica de longitude.
 * @param largura   Largura total da imagem de destino em pixels.
 * @param altura    Altura total da imagem de destino em pixels.
 * @param projecao  Caractere que identifica a projeção a ser aplicada.
 * @return Uma estrutura CoordenadaXY com as posições 'x' e 'y' arredondadas para pixels.
 */
CoordenadaXY converter_geo_pixel(double latitude, double longitude, int largura, int altura, char projecao) {
    CoordenadaXY centro = { largura / 2.0, altura / 2.0 };
    CoordenadaXY xy;
    double modulo;
    double ratio = (double)largura / altura;
    double x_proj, y_proj, dummy;

    switch (projecao) {
        case 'e':
            calcular_eckert_iv(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.002) { calcular_eckert_iv(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_eckert_iv(89.941328, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'E':
            calcular_eckert_vi(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.0) { calcular_eckert_vi(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_eckert_vi(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'g':
            calcular_gott_equal_area(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.621) { calcular_gott_equal_area(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_gott_equal_area(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'G':
            calcular_gott_azimuthal(latitude, longitude, &x_proj, &y_proj);
            modulo = (ratio < 1.0) ? largura / (sin(0.446 * PI) * 2) : altura / (sin(0.446 * PI) * 2);
            break;
        case 'h':
            calcular_hammer(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.0) { calcular_hammer(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_hammer(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'i':
            calcular_equal_earth(converter_geografica_para_autalica(latitude), longitude, &x_proj, &y_proj);
            if (ratio < 2.05) { calcular_equal_earth(converter_geografica_para_autalica(0), 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_equal_earth(converter_geografica_para_autalica(90), 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'k':
            calcular_kavrayskiy_vii(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.733) { calcular_kavrayskiy_vii(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_kavrayskiy_vii(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'l':
            calcular_lambert_azimuthal(latitude, longitude, &x_proj, &y_proj);
            modulo = (ratio < 1.0) ? largura / 4.0 : altura / 4.0;
            break;
        case 'm':
            calcular_miller(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.363) { calcular_miller(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_miller(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'M':
            calcular_mollweide(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.0) { calcular_mollweide(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_mollweide(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'n':
            calcular_natural_earth(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.923) { calcular_natural_earth(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_natural_earth(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'N':
            calcular_natural_earth_ii(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.869) { calcular_natural_earth_ii(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_natural_earth_ii(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'p':
            calcular_patterson(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.755) { calcular_patterson(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_patterson(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'r':
            calcular_robinson(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.969) { calcular_robinson(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_robinson(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 's':
            calcular_sinusoidal(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.0) { calcular_sinusoidal(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_sinusoidal(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 't':
            calcular_mercator(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.0) { calcular_mercator(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_mercator(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'w':
            calcular_wagner_vi(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 2.0) { calcular_wagner_vi(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_wagner_vi(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'W':
            calcular_winkel_iii(latitude, longitude, &x_proj, &y_proj);
            if (ratio < 1.637) { calcular_winkel_iii(0, 180, &modulo, &dummy); modulo = largura / (modulo * 2); }
            else { calcular_winkel_iii(90, 0, &dummy, &modulo); modulo = altura / (modulo * 2); }
            break;
        case 'c':
        default:
            x_proj = longitude; // * (PI / 180.0);
            y_proj = latitude; // * (PI / 180.0);
            if (ratio < 2.0) modulo = largura / 360.0;
            else modulo = altura / 180.0;
            break;
    }

    xy.x = floor(centro.x + (x_proj * modulo));
    xy.y = floor(centro.y - (y_proj * modulo));
    return xy;
}

/**
 * @brief Identifica o caractere da projeção a partir do nome.
 *
 * Compara a string de entrada com os nomes por extenso das projeções suportadas
 * e devolve o respetivo caractere identificador único. Se a entrada já for um único
 * caractere, devolve-o diretamente.
 *
 * @param nome String contendo o nome da projeção ou o seu caractere identificador.
 * @return O caractere ('a'-'Z') correspondente à projeção (ex: 'c' para PlateCarree).
 */
char identificar_projecao(const char* nome) {
    if (strlen(nome) == 1) return nome[0];
    if (!strcmp(nome, "EckertIV")) return 'e';
    if (!strcmp(nome, "EckertVI")) return 'E';
    if (!strcmp(nome, "EqualEarth")) return 'i';
    if (!strcmp(nome, "GottEqualArea")) return 'g';
    if (!strcmp(nome, "GottAzimuthal")) return 'G';
    if (!strcmp(nome, "Hammer")) return 'h';
    if (!strcmp(nome, "KavrayskiyVII")) return 'k';
    if (!strcmp(nome, "Lambert")) return 'l';
    if (!strcmp(nome, "Mercator")) return 't';
    if (!strcmp(nome, "Miller")) return 'm';
    if (!strcmp(nome, "Mollweide")) return 'M';
    if (!strcmp(nome, "NaturalEarth")) return 'n';
    if (!strcmp(nome, "NaturalEarthII")) return 'N';
    if (!strcmp(nome, "Patterson")) return 'p';
    if (!strcmp(nome, "PlateCarree")) return 'c';
    if (!strcmp(nome, "Robinson")) return 'r';
    if (!strcmp(nome, "Sinusoidal")) return 's';
    if (!strcmp(nome, "WagnerVI")) return 'w';
    if (!strcmp(nome, "WinkelTripel")) return 'W';
    return 'c';
}

/**
 * @brief Obtém o nome completo da projeção a partir do seu caractere identificador.
 *
 * Faz o caminho inverso da função identificar_projecao, devolvendo a string literal
 * com o nome oficial do sistema de projeção.
 *
 * @param id Caractere identificador da projeção.
 * @return String constante com o nome completo da projeção.
 */
const char* obter_nome_projecao(char id) {
    switch (id) {
        case 'e': return "EckertIV";
        case 'E': return "EckertVI";
        case 'g': return "GottEqualArea";
        case 'G': return "GottAzimuthal";
        case 'h': return "Hammer";
        case 'i': return "EqualEarth";
        case 'k': return "KavrayskiyVII";
        case 'l': return "Lambert";
        case 'm': return "Miller";
        case 'M': return "Mollweide";
        case 'n': return "NaturalEarth";
        case 'N': return "NaturalEarthII";
        case 'p': return "Patterson";
        case 'r': return "Robinson";
        case 's': return "Sinusoidal";
        case 't': return "Mercator";
        case 'w': return "WagnerVI";
        case 'W': return "WinkelTripel";
        case 'c':
        default:  return "PlateCarree";
    }
}

/**
 * @brief Lê o ficheiro de configuração oculto do utilizador.
 *
 * Procura o ficheiro `.mapa-mundi.conf` no diretório HOME do utilizador
 * para carregar os caminhos predefinidos dos ficheiros CSV (cidades) e GeoJSON (continentes).
 * Em caso de falha na leitura, encerra o programa com erro.
 */
void ler_configuracao() {
    char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "Erro: Variável de ambiente HOME não encontrada.\n");
        exit(EXIT_FAILURE);
    }
    
    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s/.mapa-mundi.conf", home);
    
    if (access(config_path, F_OK) != 0) {
        fprintf(stderr, "Erro: Arquivo de configuração não encontrado: %s\n", config_path);
        fprintf(stderr, "Crie o arquivo com os caminhos para CSV_PATH e GEOJSON_PATH.\n");
        exit(EXIT_FAILURE);
    }
    
    FILE *file = fopen(config_path, "r");
    if (!file) {
        fprintf(stderr, "Erro: Não foi possível ler o arquivo de configuração: %s\n", config_path);
        exit(EXIT_FAILURE);
    }
    
    char linha[MAX_LINE];
    while (fgets(linha, sizeof(linha), file)) {
        if (strncmp(linha, "CSV_PATH=", 9) == 0) sscanf(linha, "CSV_PATH=%[^\n]", csv_path);
        else if (strncmp(linha, "GEOJSON_PATH=", 13) == 0) sscanf(linha, "GEOJSON_PATH=%[^\n]", geojson_path);
    }
    fclose(file);
    
    if (strlen(csv_path) == 0 || access(csv_path, F_OK) != 0) {
        fprintf(stderr, "Erro: Arquivo CSV não encontrado ou caminho inválido.\n");
        fprintf(stderr, "Caminho lido: '%s'\n", csv_path);
        exit(EXIT_FAILURE);
    }
    
    if (strlen(geojson_path) == 0 || access(geojson_path, F_OK) != 0) {
        fprintf(stderr, "Erro: Arquivo GeoJSON não encontrado ou caminho inválido.\n");
        fprintf(stderr, "Caminho lido: '%s'\n", geojson_path);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Processa o ficheiro GeoJSON e desenha os polígonos dos continentes.
 *
 * Carrega a geometria do mapa global (Polygon e MultiPolygon), valida as
 * coordenadas de cada vértice, converte-as para o plano bidimensional do ecrã
 * e escreve as respetivas tags `<polygon>` no ficheiro SVG.
 *
 * @param svg Ponteiro para o ficheiro SVG aberto em modo de escrita.
 */
void desenhar_continentes_geojson(FILE *svg) {
    FILE *geo = fopen(geojson_path, "rb");
    if (!geo) {
        fprintf(stderr, "Erro ao abrir o arquivo GeoJSON: %s\n", geojson_path);
        return;
    }

    fseek(geo, 0, SEEK_END);
    long length = ftell(geo);
    fseek(geo, 0, SEEK_SET);

    char *data = (char *)malloc(length + 1);
    if (!data) {
        fprintf(stderr, "Erro de alocação de memória para o GeoJSON.\n");
        fclose(geo);
        return;
    }

    fread(data, 1, length, geo);
    data[length] = '\0';
    fclose(geo);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Erro no parse do JSON antes de: %s\n", error_ptr);
        }
        return;
    }

    cJSON *features = cJSON_GetObjectItemCaseSensitive(json, "features");
    if (!cJSON_IsArray(features)) {
        cJSON_Delete(json);
        return;
    }

    fprintf(svg, "<g fill=\"rgb(200,200,200)\" stroke=\"rgb(152,152,152)\" stroke-width=\"1\">\n");

    cJSON *feature;
    cJSON_ArrayForEach(feature, features) {
        cJSON *geometry = cJSON_GetObjectItemCaseSensitive(feature, "geometry");
        if (!geometry || cJSON_IsNull(geometry)) continue;

        cJSON *type = cJSON_GetObjectItemCaseSensitive(geometry, "type");
        cJSON *coordinates = cJSON_GetObjectItemCaseSensitive(geometry, "coordinates");

        if (!cJSON_IsString(type) || !cJSON_IsArray(coordinates)) continue;

        if (strcmp(type->valuestring, "Polygon") == 0) {
            cJSON *ring;
            cJSON_ArrayForEach(ring, coordinates) {
                fprintf(svg, "<polygon points=\"");
                cJSON *coord;
                cJSON_ArrayForEach(coord, ring) {
                    if (cJSON_GetArraySize(coord) >= 2) {
                        double lon = cJSON_GetArrayItem(coord, 0)->valuedouble;
                        double lat = cJSON_GetArrayItem(coord, 1)->valuedouble;

                        if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
                            CoordenadaXY xy = converter_geo_pixel(lat, lon, img_largura, img_altura, projecao_cod);
                            fprintf(svg, "%.0f,%.0f ", xy.x, xy.y);
                        }
                    }
                }
                fprintf(svg, "\" />\n");
            }
        }
        else if (strcmp(type->valuestring, "MultiPolygon") == 0) {
            cJSON *polygon;
            cJSON_ArrayForEach(polygon, coordinates) {
                cJSON *ring;
                cJSON_ArrayForEach(ring, polygon) {
                    fprintf(svg, "<polygon points=\"");
                    cJSON *coord;
                    cJSON_ArrayForEach(coord, ring) {
                        if (cJSON_GetArraySize(coord) >= 2) {
                            double lon = cJSON_GetArrayItem(coord, 0)->valuedouble;
                            double lat = cJSON_GetArrayItem(coord, 1)->valuedouble;

                            if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
                                CoordenadaXY xy = converter_geo_pixel(lat, lon, img_largura, img_altura, projecao_cod);
                                fprintf(svg, "%.0f,%.0f ", xy.x, xy.y);
                            }
                        }
                    }
                    fprintf(svg, "\" />\n");
                }
            }
        }
    }

    fprintf(svg, "</g>\n");
    cJSON_Delete(json);
}

/**
 * @brief Callback chamado pela libcsv ao processar uma coluna.
 *
 * Acumula e extrai os valores de latitude e longitude das colunas adequadas
 * temporariamente na estrutura de contexto (ContextoCSV).
 *
 * @param s    Ponteiro para a string contendo os dados do campo lido.
 * @param len  Tamanho do conteúdo lido.
 * @param data Ponteiro de contexto customizado (ContextoCSV) partilhado com a libcsv.
 */
void cb_campo(void *s, size_t len, void *data) {
    ContextoCSV *ctx = (ContextoCSV *)data;

    if (ctx->is_header) {
        ctx->coluna_atual++;
        return;
    }

    if (ctx->coluna_atual == 2 || ctx->coluna_atual == 3) {
        char buffer_temp[64];
        size_t copy_len = len < 63 ? len : 63;
        strncpy(buffer_temp, (char *)s, copy_len);
        buffer_temp[copy_len] = '\0';

        if (ctx->coluna_atual == 2) {
            ctx->lat = atof(buffer_temp);
        } else if (ctx->coluna_atual == 3) {
            ctx->lon = atof(buffer_temp);
        }
    }

    ctx->coluna_atual++;
}

/**
 * @brief Callback chamado pela libcsv ao terminar a leitura de uma linha.
 *
 * Valida rigorosamente as coordenadas lidas. Caso as coordenadas sejam válidas
 * no planeta Terra (-90 a 90 e -180 a 180), converte-as em pixels e desenha um
 * pequeno círculo (`<circle>`) no ficheiro SVG representando a cidade.
 *
 * @param c    O caractere que disparou o fim da linha (geralmente '\n').
 * @param data Ponteiro de contexto customizado (ContextoCSV) partilhado com a libcsv.
 */
void cb_linha(int c, void *data) {
    ContextoCSV *ctx = (ContextoCSV *)data;

    if (ctx->is_header) {
        ctx->is_header = 0;
        ctx->coluna_atual = 0;
        return;
    }

    if (ctx->coluna_atual > 3) {
        if (ctx->lat >= -90.0 && ctx->lat <= 90.0 && ctx->lon >= -180.0 && ctx->lon <= 180.0) {
            CoordenadaXY xy = converter_geo_pixel(ctx->lat, ctx->lon, img_largura, img_altura, projecao_cod);
            fprintf(ctx->svg, "<circle cx=\"%.0f\" cy=\"%.0f\" r=\"%d\"/>\n", xy.x, xy.y, ctx->raio);
        }
    }

    ctx->coluna_atual = 0;
    ctx->lat = 200.0;
    ctx->lon = 200.0;
}

/**
 * @brief Desenha os pontos das cidades principais lendo os dados de um ficheiro CSV.
 *
 * Abre o ficheiro CSV parametrizado, inicializa o parser da biblioteca `libcsv`
 * e lê o ficheiro em blocos, desenhando os respetivos marcadores das cidades no SVG.
 *
 * @param svg Ponteiro para o ficheiro SVG aberto em modo de escrita.
 */
void desenhar_cidades_csv(FILE *svg) {
    FILE *csv_file = fopen(csv_path, "rb");
    if (!csv_file) {
        fprintf(stderr, "Erro ao abrir o arquivo CSV: %s\n", csv_path);
        return;
    }

    fprintf(svg, "<g fill=\"rgb(33,42,116)\" stroke-width=\"0\">\n");

    int r = (int)round(img_largura * 0.003);
    if (r < 1) r = 1;

    struct csv_parser parser;
    if (csv_init(&parser, 0) != 0) {
        fprintf(stderr, "Erro ao inicializar o parser CSV.\n");
        fclose(csv_file);
        return;
    }

    ContextoCSV ctx;
    ctx.svg = svg;
    ctx.coluna_atual = 0;
    ctx.is_header = 1;
    ctx.lat = 200.0;
    ctx.lon = 200.0;
    ctx.raio = r;

    char buffer[1024];
    size_t bytes_lidos;

    while ((bytes_lidos = fread(buffer, 1, sizeof(buffer), csv_file)) > 0) {
        if (csv_parse(&parser, buffer, bytes_lidos, cb_campo, cb_linha, &ctx) != bytes_lidos) {
            fprintf(stderr, "Erro de parseamento CSV: %s\n", csv_strerror(csv_error(&parser)));
            break;
        }
    }

    csv_fini(&parser, cb_campo, cb_linha, &ctx);
    csv_free(&parser);

    fprintf(svg, "</g>\n");
    fclose(csv_file);
}

/**
 * @brief Desenha as linhas de grade (paralelos, meridianos, trópicos e círculos).
 *
 * Renderiza os caminhos (`<path>`) no ficheiro SVG representando os paralelos e meridianos
 * a cada 15 graus. Inclui também o Equador, o Meridiano de Greenwich e as linhas
 * tracejadas representativas dos Trópicos (Câncer/Capricórnio) e Círculos Polares.
 *
 * @param svg Ponteiro para o ficheiro SVG aberto em modo de escrita.
 */
void desenhar_linhas_grade(FILE *svg) {
    int passo_curva = 2;

    fprintf(svg, "<g fill=\"none\" stroke=\"rgb(240,240,240)\" stroke-width=\"1\">\n");

    for (int lat = -75; lat <= 75; lat += 15) {
        if (lat == 0) continue;

        CoordenadaXY xy = converter_geo_pixel(lat, -180.0, img_largura, img_altura, projecao_cod);
        fprintf(svg, "<path d=\"M%.0f,%.0f", xy.x, xy.y);
        for (int lon = -180 + passo_curva; lon <= 180; lon += passo_curva) {
            xy = converter_geo_pixel(lat, lon, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }
        fprintf(svg, "\" />\n");
    }

    for (int lon = -180; lon <= 180; lon += 15) {
        CoordenadaXY xy = converter_geo_pixel(-90.0, lon, img_largura, img_altura, projecao_cod);
        fprintf(svg, "<path d=\"M%.0f,%.0f", xy.x, xy.y);
        for (int lat = -90 + passo_curva; lat <= 90; lat += passo_curva) {
            xy = converter_geo_pixel(lat, lon, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }
        fprintf(svg, "\" />\n");
    }

    CoordenadaXY xy_eq = converter_geo_pixel(0, -180.0, img_largura, img_altura, projecao_cod);
    fprintf(svg, "<path d=\"M%.0f,%.0f", xy_eq.x, xy_eq.y);
    for (int lon = -180 + passo_curva; lon <= 180; lon += passo_curva) {
        xy_eq = converter_geo_pixel(0, lon, img_largura, img_altura, projecao_cod);
        fprintf(svg, " L%.0f,%.0f", xy_eq.x, xy_eq.y);
    }
    fprintf(svg, "\" />\n");

    fprintf(svg, "</g>\n");

    double circulos[] = {23.43656, -23.43656, 66.5622, -66.5622};

    fprintf(svg, "<g fill=\"none\" stroke=\"rgb(240,240,240)\" stroke-width=\"1\" stroke-dasharray=\"10,10\">\n");

    for (int i = 0; i < 4; i++) {
        double lat = circulos[i];
        CoordenadaXY xy = converter_geo_pixel(lat, -180.0, img_largura, img_altura, projecao_cod);
        fprintf(svg, "<path d=\"M%.0f,%.0f", xy.x, xy.y);
        for (int lon = -180 + passo_curva; lon <= 180; lon += passo_curva) {
            xy = converter_geo_pixel(lat, lon, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }
        fprintf(svg, "\" />\n");
    }

    fprintf(svg, "</g>\n");
}

/**
 * @brief Desenha o fundo azul (oceano) delimitado pelo perímetro da projeção.
 *
 * Renderiza um elemento SVG (um `<circle>` para projeções azimutais ou um `<path>`
 * para as restantes) que serve como silhueta da Terra e fundo para o oceano.
 *
 * @param svg Ponteiro para o ficheiro SVG aberto em modo de escrita.
 */
void desenhar_fundo_azul(FILE *svg) {
    if (projecao_cod == 'l' || projecao_cod == 'G') {
        CoordenadaXY centro = { img_largura / 2.0, img_altura / 2.0 };
        double raio;
        if (((double)img_largura / img_altura) < 1.0) {
            raio = round(img_largura / 2.0);
        } else {
            raio = round(img_altura / 2.0);
        }
        fprintf(svg, "<circle cx=\"%.0f\" cy=\"%.0f\" r=\"%.0f\" fill=\"rgb(174,214,241)\" />\n", centro.x, centro.y, raio);
    }
    else {
        fprintf(svg, "<path d=\"");
        int primeiro = 1;
        int passo_curva = 2;

        for (int lon = -180; lon <= 180; lon += passo_curva) {
            CoordenadaXY xy = converter_geo_pixel(90.0, (double)lon, img_largura, img_altura, projecao_cod);
            if (primeiro) {
                fprintf(svg, "M%.0f,%.0f", xy.x, xy.y);
                primeiro = 0;
            } else {
                fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
            }
        }

        for (int lat = 90; lat >= -90; lat -= passo_curva) {
            CoordenadaXY xy = converter_geo_pixel((double)lat, 180.0, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }

        for (int lon = 180; lon >= -180; lon -= passo_curva) {
            CoordenadaXY xy = converter_geo_pixel(-90.0, (double)lon, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }

        for (int lat = -90; lat <= 90; lat += passo_curva) {
            CoordenadaXY xy = converter_geo_pixel((double)lat, -180.0, img_largura, img_altura, projecao_cod);
            fprintf(svg, " L%.0f,%.0f", xy.x, xy.y);
        }

        fprintf(svg, " Z\" fill=\"rgb(174,214,241)\" />\n");
    }
}

/**
 * @brief Ponto de entrada principal da aplicação.
 *
 * Analisa as flags recebidas por linha de comando (`getopt`), inicializa
 * as configurações, determina o nome do ficheiro de saída e invoca
 * sequencialmente as rotinas de desenho para compor o mapa-múndi no ficheiro SVG.
 *
 * @param argc Número de argumentos passados via linha de comando.
 * @param argv Matriz contendo as strings dos argumentos.
 * @return EXIT_SUCCESS caso termine com sucesso, ou EXIT_FAILURE em caso de erro.
 */
int main(int argc, char *argv[]) {
    if (argc == 1) {
        exibir_ajuda(argv[0]);
        return EXIT_SUCCESS;
    }

    int opt;
    char projecao_nome[50] = "PlateCarree";
    int opt_desenhar_cidades = 0;
    int opt_desenhar_grade = 0;
    
    ler_configuracao();
    
    while ((opt = getopt(argc, argv, "t:p:o:hcl")) != -1) {
        switch (opt) {
            case 't': sscanf(optarg, "%dx%d", &img_largura, &img_altura); break;
            case 'p': snprintf(projecao_nome, sizeof(projecao_nome), "%s", optarg); break;
            case 'o': snprintf(output_file, sizeof(output_file), "%s", optarg); break;
            case 'c': opt_desenhar_cidades = 1; break;
            case 'l': opt_desenhar_grade = 1; break;
            case 'h': 
                exibir_ajuda(argv[0]); 
                return EXIT_SUCCESS;
            default:
                exibir_ajuda(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    projecao_cod = identificar_projecao(projecao_nome);

    const char *nome_completo = obter_nome_projecao(projecao_cod);

    if (strlen(output_file) == 0) {
        snprintf(output_file, sizeof(output_file), "%s.svg", nome_completo);
    }

    FILE *svg = fopen(output_file, "w");
    if (!svg) {
        perror("Erro ao criar o arquivo SVG");
        return EXIT_FAILURE;
    }
    
    fprintf(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">\n", img_largura, img_altura);

    desenhar_fundo_azul(svg);
    desenhar_continentes_geojson(svg);

    if (opt_desenhar_cidades) desenhar_cidades_csv(svg);
    if (opt_desenhar_grade) desenhar_linhas_grade(svg);

    fprintf(svg, "</svg>\n");
    fclose(svg);
    
    printf("Mapa-múndi projetado (%s) salvo em: %s\n", nome_completo, output_file);
    return EXIT_SUCCESS;
}
