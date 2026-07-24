# Makefile — api-client-github (Fase 3: implementación en C)
#
# Targets:
#   make / make build   -> compila el binario final en build/github-client
#   make smoke-test      -> Sprint 3.1: confirma que libcurl, sqlite3 y cJSON
#                            están instalados/linkean y que models.h compila.
#   make test-unit        -> Tests offline (sin red), fixtures/JSON de
#                            ejemplo armados a mano: http_client (Sprint
#                            3.2) + json_parser (Sprint 3.3). Rápido y
#                            determinístico — correr siempre.
#   make test-live         -> Sprint 3.2: tests de integración contra la
#                            API real de api.github.com (sin token, pool
#                            de 60 req/hora). Requiere red.
#   make clean           -> borra build/
#
# Dependencias de sistema (Ubuntu/Debian):
#   apt-get install libcurl4-openssl-dev libsqlite3-dev libcjson-dev
#
# Se resuelven vía pkg-config en lugar de hardcodear -lcurl/-lsqlite3/
# -lcjson a mano: pkg-config-libcjson agrega -I/usr/include/cjson, que no
# es un path estándar de include — sin ese -I, `#include <cJSON.h>` no
# compilaría sin importar cómo se linkee. Usar pkg-config para las tres
# libs (aunque libcurl y sqlite3 no lo necesiten estrictamente) mantiene
# un único mecanismo consistente en vez de mezclar dos estilos.

CC      := gcc
STD     := -std=c17
# -std=c17 es ISO C estricto: glibc esconde funciones POSIX (strdup,
# strtok_r, etc., que se van a necesitar en los sprints siguientes) detras
# de un feature-test macro cuando no detecta soporte GNU/POSIX explicito.
# Sin esto, gcc declara strdup() implicitamente como si devolviera int
# (-Wimplicit-function-declaration), lo que trunca el puntero de 64 bits
# que en realidad devuelve y crashea en runtime -exactamente lo que paso
# al probar este Makefile la primera vez (ver smoke_test). 200809L pide
# POSIX.1-2008 explicitamente, en vez de recurrir a -std=gnu17 (que
# habilitaria TODAS las extensiones GNU de una, no solo POSIX).
FEATURES := -D_POSIX_C_SOURCE=200809L
WARN    := -Wall -Wextra -Wpedantic
DEBUG   := -g

PKGS       := libcurl sqlite3 libcjson
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))

CFLAGS  := $(STD) $(FEATURES) $(WARN) $(DEBUG) -Iinclude $(PKG_CFLAGS)
LDLIBS  := $(PKG_LIBS)

SRC_DIR   := src
BUILD_DIR := build
TEST_DIR  := tests/c

TARGET  := $(BUILD_DIR)/github-client
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all build clean smoke-test check-deps test-unit test-live

all: build

check-deps:
	@pkg-config --exists $(PKGS) && echo "OK: libcurl, sqlite3 y libcjson disponibles via pkg-config." \
	  || (echo "Falta alguna dependencia. Instalá con:"; \
	      echo "  apt-get install libcurl4-openssl-dev libsqlite3-dev libcjson-dev"; exit 1)

build: $(TARGET)

# Nota: $(BUILD_DIR) (= "build") NO se declara como target/prerequisito de
# make (ej. "| $(BUILD_DIR)"): al ser tambien el nombre del target phony
# "build" de arriba, make los confunde (mismo nombre literal) y arma una
# dependencia circular build/github-client -> build -> build/github-client.
# Por eso el mkdir se hace a mano en cada receta que escribe dentro de
# build/, en vez de modelarlo como dependencia de make.

# A esta altura (Sprint 3.1) src/ solo tiene models.c: "make build" falla
# a proposito con "undefined reference to `main'" -no hay CLI todavia,
# eso es el Sprint 3.6-. Es el comportamiento esperado, no un bug del
# Makefile. Para validar el setup de este sprint (libs + models.h) usar
# "make smoke-test", que no depende de que exista un main.
$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

smoke-test: $(BUILD_DIR)/smoke_test
	./$(BUILD_DIR)/smoke_test

$(BUILD_DIR)/smoke_test: $(TEST_DIR)/smoke_test.c $(SRC_DIR)/models.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/smoke_test.c $(SRC_DIR)/models.c $(LDLIBS)

# Cada suite de test es su propio binario (no reusan $(OBJECTS)/$(TARGET)
# a propósito, para poder correr cualquiera sin depender de que exista un
# main.c real, que recién llega en el Sprint 3.6).

# Sprint 3.2 — http_client (offline + en vivo)
test-unit: $(BUILD_DIR)/test_http_client_unit $(BUILD_DIR)/test_json_parser_unit
	./$(BUILD_DIR)/test_http_client_unit
	./$(BUILD_DIR)/test_json_parser_unit

$(BUILD_DIR)/test_http_client_unit: $(TEST_DIR)/test_http_client_unit.c $(SRC_DIR)/http_client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/test_http_client_unit.c $(SRC_DIR)/http_client.c $(LDLIBS)

test-live: $(BUILD_DIR)/test_http_client_live
	./$(BUILD_DIR)/test_http_client_live

$(BUILD_DIR)/test_http_client_live: $(TEST_DIR)/test_http_client_live.c $(SRC_DIR)/http_client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/test_http_client_live.c $(SRC_DIR)/http_client.c $(LDLIBS)

# Sprint 3.3 — json_parser (offline). Linkea tambien http_client.c porque
# ahi vive github_error_set() (Sprint 3.2, expuesta para que json_parser.c
# no tenga que reinventar su propio helper de armado de errores) y
# models.c porque json_parser puebla un RepoInfo real, no un stub.
$(BUILD_DIR)/test_json_parser_unit: $(TEST_DIR)/test_json_parser_unit.c $(SRC_DIR)/json_parser.c \
                                     $(SRC_DIR)/models.c $(SRC_DIR)/http_client.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/test_json_parser_unit.c $(SRC_DIR)/json_parser.c \
		$(SRC_DIR)/models.c $(SRC_DIR)/http_client.c $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR)

