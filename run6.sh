docker build -t calico/bird6:latest .
docker rm -f calico-bird6
docker run --name calico-bird6 --volumes-from calico-confd calico/bird6:latest bird6 -R -s bird6.ctl -d -c /config/bird6.cfg
