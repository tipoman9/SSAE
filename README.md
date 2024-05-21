For details please see https://github.com/OpenIPC/majestic-plugins

## Compile
Build OpenIPC from source, assuming here /home/home/src/ssc30kq/openipc-firmware.

Copy contents over /home/home/src/ssc30kq/openipc-firmware/output/build/majestic-plugins-HEAD/

```
/usr/bin/make -j9 -C /home/home/src/ssc30kq/openipc-firmware/output/ majestic-plugins-rebuild BOARD=ssc30kq_fpv
```

### Upload plugin file to camera:

```
scp -O /home/home/src/ssc30kq/openipc-firmware/output/build/majestic-plugins-HEAD/*.so root@192.168.1.88:/usr/lib
```


## Custom functions
### customAE
expects two numbers separated by comma, first rate in second to read the stats from the sensor, second is max percent change.
customAE 20,5 means 20fps, max 10 percent exposure change per frame.
```
echo customAE 20,5 | nc localhost 4000
```
First param is "Max ISP Frame Rate"
This is the frame rate that the AutoExposure function will read the exposure info from the ISP. In the default 3A implemntation, it is fixed to sensor refresh rate (not the encoder rate!).
Set this value to lower than 30. Very low value will limit the rate the AE is calculated and applied.

Second param is "MaxAERatioChange", default is 10.
This is the Exposure change ratio per cicle. Set it to values between 10 and 30. 
Large values will make the AE react quickly, but with noticeable "blink" or "step".
Lower values will have smooth but slow transition.


### stop3a
```echo stopAE 100 | nc localhost 4000```



## To run
### First enable plugin support 
cli -s .system.plugins true
Copy .so file to /usr/lib

### To start, when majestic is running
echo stopAE 1 | nc localhost 4000
echo customAE 25,7 | nc localhost 4000

### To reload majestic and apply CustomAE
echo stopAE 1 | nc localhost 4000
sleep 0.1
killall -1 majestic
sleep 2
echo customAE 25,7 | nc localhost 4000
