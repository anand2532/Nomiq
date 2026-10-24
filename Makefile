# Host-side checks (no hardware, no ESP-IDF needed). Used by CI and the engineer agents.
.PHONY: test test-proto test-storage tools clean
test: test-proto test-storage test-radio test-stt test-camera test-app test-ui test-audio test-power tools

test-proto:
	$(MAKE) -C firmware/components/nomiq_proto/test

test-storage:
	$(MAKE) -C firmware/components/storage/test

test-radio:
	$(MAKE) -C firmware/components/radio_sx1262/test

test-power:
	$(MAKE) -C firmware/components/power/test

test-audio:
	$(MAKE) -C firmware/components/audio/test

test-ui:
	$(MAKE) -C firmware/components/ui/test

test-app:
	$(MAKE) -C firmware/components/app_core/test

test-camera:
	$(MAKE) -C firmware/components/camera/test

test-stt:
	$(MAKE) -C firmware/components/stt_link/test
	cd tools/stt-bench && python3 -m unittest -q test_wer && python3 bench.py --self-test > /dev/null
	cd tools/stt-coproc && python3 -m unittest -q test_coproc

tools:
	python3 tools/lora_airtime.py --table > /dev/null
	bash -n tools/gitbot/gitflow.sh tools/gitbot/publish_queue.sh
	python3 -m json.tool .mcp.json > /dev/null
	python3 hardware/design/nomiq_hw.py --check
	python3 hardware/design/gen_kicad.py --check
	python3 hardware/design/fplib.py --check
	python3 hardware/design/pcb_check.py

clean:
	$(MAKE) -C firmware/components/nomiq_proto/test clean
	$(MAKE) -C firmware/components/storage/test clean
	$(MAKE) -C firmware/components/radio_sx1262/test clean
	$(MAKE) -C firmware/components/stt_link/test clean
	$(MAKE) -C firmware/components/camera/test clean
	$(MAKE) -C firmware/components/app_core/test clean
	$(MAKE) -C firmware/components/ui/test clean
	$(MAKE) -C firmware/components/audio/test clean
	$(MAKE) -C firmware/components/power/test clean
