CC = gcc
CFLAGS = -Wall -Wextra -O3
LDFLAGS = -lm -lcjson -lcsv

BINDIR = bin
TARGET = $(BINDIR)/mapamundi
SRC = src/mapamundi.c

.PHONY: all clean install-conf

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

install-conf:
	mkdir -p $(HOME)/.Mapa-mundi
	cp -f lib/worldcities_capital.csv $(HOME)/.Mapa-mundi/
	cp -f lib/ne_110m_admin_0_countries_lakes.geojson $(HOME)/.Mapa-mundi/
	printf "CSV_PATH=%s/.Mapa-mundi/worldcities_capital.csv\nGEOJSON_PATH=%s/.Mapa-mundi/ne_110m_admin_0_countries_lakes.geojson\n" "$(HOME)" "$(HOME)" > $(HOME)/.mapa-mundi.conf

clean:
	rm -f $(TARGET)
