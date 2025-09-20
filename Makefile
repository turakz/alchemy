clean:
	@if [ -e compile_commands.json ]; then \
		echo "LightningStructs::removing compile_commands.json..."; \
		rm compile_commands.json; \
	fi
	rm -rf build
	rm -rf .cache

.PHONY: setup
setup:
	@echo "LightningStructs::detecting OS..."
	@sh -c 'OS=$$OS; UNAME_S=$$(uname -s); \
	if [ "$$OS" = "Windows_NT" ]; then \
		echo "LightningStructs::windows detected, installing with choco..."; \
		choco install llvm cmake ninja -y; \
	elif [ "$$UNAME_S" = "Linux" ]; then \
		echo "LightningStructs::linux detected, installing with apt..."; \
		sudo apt install -y clang llvm-dev libclang-dev cmake build-essential ninja-build; \
	elif [ "$$UNAME_S" = "Darwin" ]; then \
		echo "LightningStructs::macos detected, installing with brew..."; \
		brew install llvm cmake ninja; \
	else \
		echo "LightningStructs::unsupported OS: $$UNAME_S"; \
		exit 1; \
	fi'

.PHONY: lstructs
lstructs:
	@echo "LightningStructs::configuring and building..."
	cmake -S. -Bbuild -GNinja
	cmake --build build
	@if [ -e build/compile_commands.json ]; then \
		echo "LightningStructs::creating symlink for build/compile_commands.json..."; \
		ln -sf build/compile_commands.json; \
	fi

.PHONY: lstructs.run
lstructs.run: lstructs
	@./build/LightningStructs
