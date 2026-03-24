################################################################################
# variables
################################################################################

# image customization
export BUILDKIT_PROGRESS=plain
export DOCKER_BUILDKIT=1
DOCKER_IMAGE := alchemy-ci
DOCKER_CONTAINER_NAME ?= alchemy-ci

# host user/group id — allows docker to set permissions of generated files
UID := $(shell id -u)
GID := $(shell id -g)
HOSTNAME := $(shell hostname)

# export bamboo environment variables to file (excludes capability vars)
$(shell env | grep -P '^bamboo_(?!capability)' > .docker.env)

# create home dir
$(shell mkdir -p /tmp/docker-alchemy/home)
# create global cache dir
$(shell mkdir -p /tmp/docker-alchemy/cache)
# create project-specific cache dir
$(shell mkdir -p /tmp/docker-alchemy/.local)

################################################################################
# docker run flags
################################################################################
DOCKER_RUN_ARGS += --name ${DOCKER_CONTAINER_NAME}
DOCKER_RUN_ARGS += --rm
# runs small init system that also forwards signals
DOCKER_RUN_ARGS += --init
# forwards bamboo environment variables
DOCKER_RUN_ARGS += --env-file=.docker.env
# makes files created in the container owned by the host user instead of root
DOCKER_RUN_ARGS += -u=${UID}:${GID}
DOCKER_RUN_ARGS += --hostname=${HOSTNAME}
# mount project directory as working directory
DOCKER_RUN_ARGS += -w/src
DOCKER_RUN_ARGS += -v${CURDIR}:/src:rw
# required to git clone in the container
DOCKER_RUN_ARGS += -v${HOME}/.ssh:${HOME}/.ssh:ro
DOCKER_RUN_ARGS += -v/etc/ssh:/etc/ssh:ro
# required for host user/group to be recognized in the container
DOCKER_RUN_ARGS += -v/etc/group:/etc/group:ro
DOCKER_RUN_ARGS += -v/etc/passwd:/etc/passwd:ro
DOCKER_RUN_ARGS += -v/etc/shadow:/etc/shadow:ro
DOCKER_RUN_ARGS += --group-add sudo
# mount cache/home directories so build tools cache results between runs
DOCKER_RUN_ARGS += -v/tmp/docker-alchemy/cache:${HOME}/.cache:rw
DOCKER_RUN_ARGS += -v/tmp/docker-alchemy/home:${HOME}:rw
DOCKER_RUN_ARGS += -v/tmp/docker-alchemy/.local:${HOME}/.local:rw

ifdef bamboo_agentWorkingDirectory
  DOCKER_RUN_ARGS += -v${bamboo_agentWorkingDirectory}:${bamboo_agentWorkingDirectory}
endif
ifdef bamboo_tmp_directory
  DOCKER_RUN_ARGS += -v${bamboo_tmp_directory}:${bamboo_tmp_directory}
endif
ifdef bamboo_working_directory
  DOCKER_RUN_ARGS += -v${bamboo_working_directory}:${bamboo_working_directory}
endif

################################################################################
# image tag
################################################################################
DOCKER_IMAGE_EXACT_VERSION := ${DOCKER_IMAGE}:ubuntu-22.04

# running docker containers of this image
DOCKER_CONTAINERS = $(shell docker ps -q --filter ancestor=${DOCKER_IMAGE_EXACT_VERSION})
