include docker/variables.mk

.PHONY: docker.build docker.run docker.kill docker.teardown docker.dumpenv

docker.build: # build the docker image
	docker build \
	  -f docker/Dockerfile \
	  -t ${DOCKER_IMAGE_EXACT_VERSION} \
	  ${DOCKER_BUILD_ARGS} \
	  .

docker.run: ## run a docker container interactively
	docker run -it ${DOCKER_RUN_ARGS} ${DOCKER_IMAGE_EXACT_VERSION} bash

docker.kill: # stop any running containers from this image
	docker stop ${DOCKER_CONTAINERS} 2>/dev/null || true

docker.teardown: docker.kill # clean up image and containers
	docker image prune --filter="dangling=true" --force
	docker rmi ${DOCKER_IMAGE_EXACT_VERSION} 2>/dev/null || true

docker.dumpenv: ## dump evaluated variables
	$(foreach v, \
	  $(shell echo "$(filter-out .VARIABLES, \
	  $(.VARIABLES))" | tr ' ' '\n' | sort), \
	  $(if $(filter file, $(origin $(v))), $(info $(shell printf "%-27s" "$(v)") = $($(v)))) \
	)
