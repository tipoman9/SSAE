
#cd /home/home/src/ssc30kq/openipc-firmware/output/
#make majestic-plugins-rebuild  BOARD=ssc30kq_fpv

/usr/bin/make -j9 -C /home/home/src/ssc30kq/openipc-firmware/output/ majestic-plugins-rebuild BOARD=ssc30kq_fpv

scp -O /home/home/src/ssc30kq/openipc-firmware/output/build/majestic-plugins-HEAD/*.so root@192.168.1.88:/usr/lib
/usr/bin/ssh -y -y -i ~/.ssh/id_rsa root@192.168.1.88 'killall majestic '

