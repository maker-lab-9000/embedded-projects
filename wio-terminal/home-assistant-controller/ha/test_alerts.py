"""Tests for ha/alerts.py. Run: python3 -m unittest discover -s ha"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import alerts  # noqa: E402


def all_good():
    """Every entity the rules read, in a healthy state."""
    st = {}
    for r in alerts.RULES:
        for kind, e, x in r.any_of:
            if e.startswith("sensor.smart_health") or e == "sensor.seagate_sde":
                st[e] = "1"
            elif e.endswith("_status"):
                st[e] = "up"
            elif e == "sensor.raid_active_disks":
                st[e] = "2"
            elif e == "sensor.raid_failed_disks":
                st[e] = "0"
            elif e.startswith(("light.", "switch.")):
                st[e] = "off"
            elif e.startswith("climate."):
                st[e] = "heat"
            elif e.startswith("binary_sensor."):
                st[e] = "off"
            elif e.startswith("camera."):
                st[e] = "idle"
            elif "battery" in e:
                st[e] = "80"
            else:
                st[e] = "20"
    return st


class Rules(unittest.TestCase):
    def test_healthy_home_has_no_alerts(self):
        self.assertEqual(alerts.active(all_good()), [])

    def test_titles_unique(self):
        titles = [r.title for r in alerts.RULES]
        self.assertEqual(len(titles), len(set(titles)))

    def test_room_thresholds_match_dashboard(self):
        st = all_good()
        st["sensor.multi_sensor_bedzimmer"] = "26"
        st["sensor.humidity_10"] = "75"
        self.assertEqual(alerts.active(st), [])  # dashboard: red above, not at
        st["sensor.multi_sensor_bedzimmer"] = "26.1"
        st["sensor.humidity_10"] = "75.4"
        self.assertEqual(alerts.active(st), ["Bedroom too warm", "Bathroom too humid"])

    def test_battery_20_or_less(self):
        st = all_good()
        st["sensor.temperature_9_battery"] = "21"
        self.assertEqual(alerts.active(st), [])
        st["sensor.temperature_9_battery"] = "20"
        self.assertEqual(alerts.active(st), ["Bathroom sensor battery low"])

    def test_offline_devices(self):
        st = all_good()
        st["light.extended_color_light_8"] = "unavailable"
        st["sensor.babyroom_2"] = "unknown"
        st["sensor.openclose_28_battery"] = "unavailable"
        self.assertEqual(sorted(alerts.active(st)),
                         ["Door sensor offline", "Moon lamp offline", "Theo's room sensor offline"])

    def test_services_down_and_no_data(self):
        st = all_good()
        st["sensor.grafana_status"] = "down"
        st["sensor.cp1_kubelet_status"] = "down"
        st["sensor.loki_status"] = "unavailable"
        self.assertEqual(alerts.active(st), ["Grafana down", "cp1 (k8s control plane): check down", "Loki: no data"])

    def test_homelab_storage(self):
        st = all_good()
        st["sensor.raid_failed_disks"] = "unavailable"   # dashboard: int(1) > 0 -> red
        st["sensor.smart_health_sda"] = "0"
        self.assertEqual(alerts.active(st), ["RAID md0 degraded", "BigData SMART check failed"])
        st = all_good()
        st["sensor.raid_active_disks"] = "1"
        self.assertEqual(alerts.active(st), ["RAID md0 degraded"])

    def test_vms(self):
        st = all_good()
        st["sensor.ram_usage"] = "unavailable"
        st["sensor.disk_use_percent"] = "91"
        self.assertEqual(alerts.active(st), ["Ubuntu VM 100: no data", "HA VM 101 above 90 %"])

    def test_gaming_pc_off_is_not_an_alert(self):
        st = all_good()
        for r in alerts.RULES:
            if r.group == "Gaming PC":
                for _, e, _ in r.any_of:
                    st[e] = "unavailable"
        self.assertEqual(alerts.active(st), [])
        st["sensor.desktop_a498ue6_amd_radeon_rx_6750_xt_gpu_hot_spot_temperature"] = "101"
        self.assertEqual(alerts.active(st), ["GPU hot spot above 100 °C"])


class Output(unittest.TestCase):
    def test_view_shape(self):
        v = alerts.view()
        self.assertEqual(v["path"], "alerts")
        self.assertEqual(v["type"], "sections")
        cards = [c for s in v["sections"] for c in s["cards"] if c["type"] == "custom:mushroom-template-card"]
        self.assertEqual(len(cards), len(alerts.RULES) + 1)  # + summary
        for c in cards[1:]:
            self.assertEqual(c["icon_color"], "red")
            self.assertEqual(len(c["visibility"]), 1)
        json.dumps(v)

    def test_visibility_conditions(self):
        r = next(r for r in alerts.RULES if r.title == "RAID md0 degraded")
        vis = alerts.rule_visibility(r)
        self.assertEqual(vis["condition"], "or")
        self.assertIn({"condition": "numeric_state", "entity": "sensor.raid_active_disks", "below": 2}, vis["conditions"])
        r = next(r for r in alerts.RULES if r.title == "NVMe SMART check failed")
        self.assertEqual(alerts.rule_visibility(r),
                         {"condition": "state", "entity": "sensor.smart_health_nvme", "state_not": "1"})

    def test_jinja_lists_every_rule(self):
        j = alerts.jinja()
        for r in alerts.RULES:
            self.assertIn(f"{r.group}|{r.title}\n", j)
        self.assertEqual(j.count("{% if"), j.count("{% endif %}"))
        self.assertIn("states('sensor.babyroom')|float(-1e9) > 26", j)


if __name__ == "__main__":
    unittest.main()
