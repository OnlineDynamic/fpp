all: BBB-AUDIO-PCM5012A-00A0.dtbo BBB-P8-AUDIO-PCM5012A-00A0.dtbo bbb-fpp-reserve-memory.dtbo fpp-cape-overlay.dtb

clean:
	rm -f BBB-AUDIO-PCM5012A-00A0.dtbo BBB-P8-AUDIO-PCM5012A-00A0.dtbo bbb-fpp-reserve-memory.dtbo fpp-cape-overlay.dtb

install:
	cp BBB-AUDIO-PCM5012A-00A0.dtbo /lib/firmware
	cp BBB-P8-AUDIO-PCM5012A-00A0.dtbo /lib/firmware
	cp bbb-fpp-reserve-memory.dtbo /lib/firmware
	cp fpp-cape-overlay.dtb /lib/firmware
	cp fpp-cape-overlay.dtb /lib/firmware/fpp-cape-overlay-default.dtb

BBB-AUDIO-PCM5012A-00A0.dtbo: BBB-AUDIO-PCM5012A-00A0.dts
	cp BBB-AUDIO-PCM5012A-00A0.dts /opt/source/bb.org-overlays/src/arm
	touch /opt/source/bb.org-overlays/src/arm/BBB-AUDIO-PCM5012A-00A0.dts
	cd /opt/source/bb.org-overlays ; make src/arm/BBB-AUDIO-PCM5012A-00A0.dtbo
	cp /opt/source/bb.org-overlays/src/arm/BBB-AUDIO-PCM5012A-00A0.dtbo .

BBB-P8-AUDIO-PCM5012A-00A0.dtbo: BBB-P8-AUDIO-PCM5012A-00A0.dts
	cp BBB-P8-AUDIO-PCM5012A-00A0.dts /opt/source/bb.org-overlays/src/arm
	touch /opt/source/bb.org-overlays/src/arm/BBB-P8-AUDIO-PCM5012A-00A0.dts
	cd /opt/source/bb.org-overlays ; make src/arm/BBB-P8-AUDIO-PCM5012A-00A0.dtbo
	cp /opt/source/bb.org-overlays/src/arm/BBB-P8-AUDIO-PCM5012A-00A0.dtbo .

bbb-fpp-reserve-memory.dtbo: bbb-fpp-reserve-memory.dts
	cp bbb-fpp-reserve-memory.dts /opt/source/bb.org-overlays/src/arm
	touch /opt/source/bb.org-overlays/src/arm/bbb-fpp-reserve-memory.dts
	cd /opt/source/bb.org-overlays ; make src/arm/bbb-fpp-reserve-memory.dtbo
	cp /opt/source/bb.org-overlays/src/arm/bbb-fpp-reserve-memory.dtbo .

fpp-cape-overlay.dtb: fpp-cape-overlay.dts
	dtc -O dtb -o fpp-cape-overlay.dtb fpp-cape-overlay.dts
