# FLIRONE Patient Temperature Monitoring

## Muc dich va pham vi

Day la ung dung C++ chay tren Raspberry Pi de thu frame nhiet tu FLIR ONE qua USB,
chuyen doi du lieu raw sang nhiet do, hien thi qua web UI va luu cac phep do spot/ROI.
He thong co them DHT12 de lay nhiet do/do am moi truong, servo de dieu huong camera
va Google Sheets de ghi nhat ky.

He thong hien tai la prototype ky thuat, khong phai thiet bi y te da duoc phe duyet va
khong duoc su dung don doc de chan doan, phan loai hay quyet dinh dieu tri.

## Nguon chinh va bien the lich su

- `src/` la active implementation, build tu `src/Makefile`.
- `palettes/` chua palette nhiet duoc nap luc khoi dong.
- `scripts/` chua cac script ho tro v4l2loopback/GStreamer.
- `backup_src/`, `backup_src_7_8/`, `backup_src_7_8_v2/` la cac bien the lich su;
	khong sua truc tiep neu chua co quyet dinh phien ban.
- `build/` va cac thu muc `components/*/build`, `components/*/lib` la artefact tao ra.

## Kien truc runtime hien tai

```text
FLIR ONE USB -> FlirOneReader -> ThermalFrame raw 16-bit
																			|
										DHT12 ambient -> process_frame(Planck/LUT)
																			|
			 ProcessedFrame -> WebServer/WebSocket (JPEG, spot, ROI, status)
											-> screenshot persistence
											-> GoogleSheetsLogger queue
			 User commands -> servo component / PCA9685
```

`main.cpp` la bo dieu phoi hien tai. Vong lap FLIR doc USB va xu ly frame dong bo;
DHT12 chay tren thread rieng; WebServer va GoogleSheetsLogger co co che thread rieng.
`config.cpp` doc JSON va cung cap default. Web UI nam trong `src/web/`.

## Du lieu va calibration

`thermal_proc.cpp` dung Planck constants va cac tham so co the dieu chinh runtime:

- `emissivity`: 0.70..1.00
- `refl_offset`: -20..20 C
- `raw_scale`: 1..6
- `temp_offset`: -10..10 C

Ambient tu DHT12 duoc dung lam nhiet do phan xa. Cac tham so nay chi la tunables ky thuat,
khong phai bang chung calibration. Moi bao cao nhiet do can luu kem raw context, ambient,
RH, tham so calibration, thoi diem, firmware/build va trang thai cam bien.

## Build va chay

Tu thu muc `src/`:

```sh
make all
sudo ./flirone ./flirone_config.json
```

Dependencies chinh: libusb-1.0, libjpeg, libwebsockets, nlohmann-json, libcurl,
pigpio va toolchain C++17. Chi tiet nam trong `src/Makefile` va `src/components/Makefile`.

Web UI mac dinh: `http://<raspberry-pi-ip>:8080`.
Service systemd: `src/flirone.service`.

## Kiem tra toi thieu truoc moi thay doi

1. `make -C src clean && make -C src all`.
2. Kiem tra khoi dong voi FLIR khong ket noi, DHT12 loi va Google Sheets khong kha dung.
3. Kiem tra ket noi lai USB, WebSocket reconnect, spot/ROI, screenshot va shutdown.
4. So sanh voi vat tham chieu co nhiet do da biet trong dieu kien moi truong duoc ghi lai.
5. Khong ket luan do chinh xac chi tu anh mau hoac mot phep do don le.

## Quan tri thay doi va tai lieu

- Guideline kien truc: `PROJECT_ARCHITECTURE_GUIDELINE_PROMPT.md`.
- System prompt supervisor/worker: `SUPERVISOR_SYSTEM_PROMPT.md`.
- Ma tran task, worker, input/output va gate: `cautruc.md`.

Sau moi thay doi code, phai cap nhat README nay neu thay doi runtime flow, API message,
config, hardware mapping, build command, calibration assumption, limitation hoac safety
behavior. Khong ghi nhan suy doan thanh tinh nang da duoc xac minh.

## Nguon tham khao ban dau

Day la cleaned-up implementation tu nghien cuu cong dong FLIR ONE Linux; credit lich su
duoc giu lai trong cac ban backup va commit history. Moi su dung lam san pham y te can bo
sung traceability, verification/validation, risk management, cybersecurity va regulatory
review phu hop.