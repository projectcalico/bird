.PHONEY: clean

# These variables can be overridden by setting an environment variable.
LOCAL_IP_ENV?=$(shell ip route get 8.8.8.8 | head -1 | cut -d' ' -f8)

default: clean calicorr.created

calicorr.created: $(BUILD_FILES)
	docker build -t calico/routereflector .
	touch calicorr.created

clean:
	-rm *.created
	-docker rm -f calico-rr
	-docker rmi -f calico/rr
