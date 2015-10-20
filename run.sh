docker build -t calico/bird:latest -f Dockerfile.v4 .
docker rm -f calico-bird
docker run --name calico-bird --net host --volumes-from calico-confd calico/bird:latest bird -R -s bird.ctl -d -c /config/bird.cfg
