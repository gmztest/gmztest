## try different net configurations

- [x] try resnet 60B128F  
- [ ] test 40B128F & 20B128F  
- [ ] compare with the 20b256f baseline  
- [x] try renext  
- [ ] try densenet  

## Find out why the hybrid is weak

- the speed of net computation (try to rewrite the network into tensorflow)  
- the value net is not accurate enough (add some comparaison to see the different)  
- the hand-crafted features of rollout, maybe more info can be found in pachi  

## the 20block-256filter baseline

Find it [here](https://drive.google.com/open?id=1PSyfsDvXmtIrhUx4Gf6x7mGzDtwe4npr)

