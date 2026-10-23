# Host-side checks (no hardware, no ESP-IDF needed). Used by CI and the engineer agents.
.PHONY: test test-proto test-storage tools clean
test: test-proto test-storage test-radio tools

test-proto:
	$(MAKE) -C firmware/components/nomiq_proto/test

test-storage:
	$(MAKE) -C firmware/components/storage/test

test-radio:
	$(MAKE) -C firmware/components/radio_sx1262/test

tools:
	python3 tools/lora_airtime.py --table > /dev/null
	bash -n tools/gitbot/gitflow.sh tools/gitbot/publish_queue.sh
	python3 -m json.tool .mcp.json > /dev/null

clean:
	$(MAKE) -C firmware/components/nomiq_proto/test clean
	$(MAKE) -C firmware/components/storage/test clean
	$(MAKE) -C firmware/components/radio_sx1262/test clean
