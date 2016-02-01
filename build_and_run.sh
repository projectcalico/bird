docker build -t calico/bird .
docker save -o calico-bird.tar calico/bird
docker2aci --debug=true  --image=calico/bird calico-bird.tar
sudo rkt fetch --insecure-options=all  calico-bird-latest.aci

#docker run --rm --name bird calico/bird



