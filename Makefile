PYTHON ?= uv run python
IDF_PY ?= python
IDF_TOOL ?= idf.py
PARTTOOL ?= $(if $(IDF_PATH),$(IDF_PATH)/components/partition_table/parttool.py,$(error Set IDF_PATH or PARTTOOL for flash-native-idf-quote-data))

QUOTE_DATA ?= data/quotes.sample.yaml
NATIVE_IDF_DIR ?= firmware/native-idf
NATIVE_IDF_APP_BIN ?= $(NATIVE_IDF_DIR)/build/quotes-clock-native.bin
NATIVE_IDF_RESCUE_BIN ?= $(NATIVE_IDF_DIR)/build/quotes-clock-native-rescue.bin
NATIVE_IDF_MAX_APP_SIZE ?= 0x190000
NATIVE_IDF_ASSETS_HPP ?= $(NATIVE_IDF_DIR)/main/generated/quotes_clock_assets.hpp
NATIVE_IDF_QUOTE_DATA ?= $(NATIVE_IDF_DIR)/main/generated/quote_data.bin
NATIVE_IDF_TLS_HPP ?= $(NATIVE_IDF_DIR)/main/generated/tls_bootstrap.hpp
NATIVE_IDF_TLS_ARGS ?=
ENV_FILE ?= .env
PORT ?=

.PHONY: quote-coverage native-idf-assets native-idf-tls native-idf-generated compile-native-idf native-idf-rescue-bin flash-native-idf flash-native-idf-quote-data check-native-idf-size release-check clean-build deploy help

quote-coverage:
	$(PYTHON) tools/validate_quotes.py $(QUOTE_DATA)
	$(PYTHON) tools/report_quote_coverage.py $(QUOTE_DATA)
	$(PYTHON) tools/report_display_text_glyphs.py $(QUOTE_DATA)

native-idf-assets:
	$(PYTHON) tools/generate_native_assets.py --input $(QUOTE_DATA) --output $(NATIVE_IDF_ASSETS_HPP) --quote-output $(NATIVE_IDF_QUOTE_DATA) --omit-quote-arrays

native-idf-tls:
	$(PYTHON) tools/generate_native_tls.py --output "$(NATIVE_IDF_TLS_HPP)" --env-file $(ENV_FILE) $(NATIVE_IDF_TLS_ARGS)

native-idf-generated: native-idf-assets native-idf-tls

compile-native-idf: native-idf-generated
	cd $(NATIVE_IDF_DIR) && $(IDF_TOOL) build

native-idf-rescue-bin: compile-native-idf
	$(IDF_PY) tools/package_native_rescue.py --project-dir $(NATIVE_IDF_DIR) --quote-data $(NATIVE_IDF_QUOTE_DATA) --output $(NATIVE_IDF_RESCUE_BIN)

flash-native-idf: compile-native-idf
	@test -n "$(PORT)" || (echo "PORT is required, for example PORT=/dev/ttyUSB0 or PORT=COM6"; exit 1)
	cd $(NATIVE_IDF_DIR) && $(IDF_TOOL) -p $(PORT) flash

flash-native-idf-quote-data: native-idf-assets
	@test -n "$(PORT)" || (echo "PORT is required, for example PORT=/dev/ttyUSB0 or PORT=COM6"; exit 1)
	cd $(NATIVE_IDF_DIR) && $(IDF_TOOL) partition-table && $(IDF_PY) "$(PARTTOOL)" --port $(PORT) write_partition --partition-name quote_data --input ../../$(NATIVE_IDF_QUOTE_DATA)

check-native-idf-size:
	$(PYTHON) tools/check_binary_size.py --file $(NATIVE_IDF_APP_BIN) --max-bytes $(NATIVE_IDF_MAX_APP_SIZE)

release-check:
	$(PYTHON) tools/check_release_tree.py --quote-data $(QUOTE_DATA)

clean-build:
	$(PYTHON) -c "import pathlib, shutil; [shutil.rmtree(pathlib.Path(p), ignore_errors=True) for p in ('firmware/native-idf/build', 'firmware/native-idf/managed_components', 'firmware/native-idf/main/generated')]; [pathlib.Path(p).unlink(missing_ok=True) for p in ('firmware/native-idf/dependencies.lock', 'firmware/native-idf/sdkconfig', 'firmware/native-idf/sdkconfig.esp32dev')]"

# Helper to extract value from .env file
define get_env_var
$(shell grep -E "^$(1)=" $(ENV_FILE) 2>/dev/null | head -1 | cut -d '=' -f2-)
endef

# Override defaults with values from .env if not already set
DEV_HOST ?= $(call get_env_var,DEV_HOST)
QUOTES_CLOCK_ADMIN_PASSWORD ?= $(call get_env_var,QUOTES_CLOCK_ADMIN_PASSWORD)

deploy: compile-native-idf
	@test -n "$(DEV_HOST)" || (echo "DEV_HOST is required"; exit 1)
	@test -n "$(QUOTES_CLOCK_ADMIN_PASSWORD)" || (echo "QUOTES_CLOCK_ADMIN_PASSWORD is required"; exit 1)
	@echo "Deploying firmware to $(DEV_HOST)..."
	@curl -k -X POST -u "admin:$(QUOTES_CLOCK_ADMIN_PASSWORD)" \
		-H "Content-Type: application/octet-stream" \
		--data-binary "@$(NATIVE_IDF_APP_BIN)" \
		"https://$(DEV_HOST)/ota-upload"
	@echo "Waiting for device to reboot..."

help:
	@echo "Makefile targets:"
	@echo "  compile-native-idf    Build the native-IDF firmware"
	@echo "  deploy                Build and OTA deploy to $(DEV_HOST) (uses QUOTES_CLOCK_ADMIN_PASSWORD from .env)"
	@echo "  flash-native-idf      Flash firmware via serial port"
	@echo "  clean-build           Remove build artifacts"
	@echo ""
	@echo "Environment variables (from .env):"
	@echo "  DEV_HOST              Device hostname or IP for OTA deploy"
	@echo "  QUOTES_CLOCK_ADMIN_PASSWORD  Admin password for device authentication"
	@echo ""
	@echo "Override defaults on command line, e.g.:"
	@echo "  make deploy DEV_HOST=my-device.local"
