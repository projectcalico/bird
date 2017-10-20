###############################################################################
# The build architecture is select by setting the ARCH variable.
# For example: When building on ppc64le you could use ARCH=ppc64le make <....>.
# When ARCH is undefined it defaults to amd64.
ARCH?=amd64
ifeq ($(ARCH),amd64)
        ARCHTAG?=
endif

ifeq ($(ARCH),ppc64le)
        ARCHTAG:=-ppc64le
endif


.PHONEY: clean

# These variables can be overridden by setting an environment variable.
LOCAL_IP_ENV?=$(shell ip route get 8.8.8.8 | head -1 | cut -d' ' -f8)

default: clean node_filesystem/confd calicorr.created

# Install Confd, for non amd64 build you must populate node_filesystem/confd befor building.
node_filesystem/confd:
	curl -L https://github.com/projectcalico/confd/releases/download/v0.12.1-calico0.2.0/confd \
	 -o node_filesystem/confd
	chmod +x node_filesystem/confd

calicorr.created: $(BUILD_FILES)
	docker build -t calico/routereflector$(ARCHTAG) -f Dockerfile$(ARCHTAG) .
	touch calicorr.created

clean:
	-rm *.created
	-docker rmi -f calico/routereflector$(ARCHTAG)
