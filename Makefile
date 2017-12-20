QMAKE_TARGET = qdropbox
PROJECT_DIR := $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))

include cs-base-library.mk
