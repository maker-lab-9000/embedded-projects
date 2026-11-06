"""Alert rules for the Home Assistant "Alerts" view (and later the Wio).

One list of rules, taken from the colours the dashboard already uses: an
item is an alert when its card would be red. Plus batteries at 20 % or less
and key devices that are unavailable. From the rules this builds:

  view()        the Lovelace "Alerts" view (mushroom cards, each visible only
                while its rule is red, and a summary card on top)
  jinja()       a template that renders one line per active alert,
                "group|title|detail" (for the Wio, via render_template)
  active(st)    the same evaluation in Python, for tests and for checking the
                template against Home Assistant before saving

Run:  python3 ha/alerts.py view|jinja
"""
import json
import sys

OFF = ["unavailable", "unknown"]


# Conditions. An atom is (kind, entity, value):
#   ("above", e, x)   numeric state > x
#   ("below", e, x)   numeric state < x
#   ("is", e, [s..])  state is one of these
#   ("not", e, s)     state is not s (also true when unavailable)
def above(e, x): return ("above", e, x)
def below(e, x): return ("below", e, x)
def is_(e, states): return ("is", e, list(states))
def not_(e, s): return ("not", e, s)


class Rule:
    def __init__(self, group, title, icon, any_of, detail, nav, entity=None):
        self.group, self.title, self.icon = group, title, icon
        self.any_of = any_of          # alert when any atom holds
        self.detail = detail          # Jinja for the card's second line
        self.nav = nav                # dashboard view the item lives in
        self.entity = entity or any_of[0][1]


def val(e, unit):
    return "{{ states('%s') }}%s" % (e, unit)


RULES = []
R = RULES.append

# Rooms (Control Panel, "Temperature / Humidity (colour preview)").
ROOMS = [("Bedroom", "sensor.multi_sensor_bedzimmer", "sensor.multi_sensor_bedzimmer_2"),
         ("Theo's room", "sensor.babyroom", "sensor.babyroom_2"),
         ("Bathroom", "sensor.temperature_9", "sensor.humidity_10"),
         ("Living room", "sensor.living_room", "sensor.living_room_2")]
for name, t, h in ROOMS:
    R(Rule("Rooms", f"{name} too warm", "mdi:thermometer-alert", [above(t, 26)],
           val(t, " °C") + " · above 26 °C", "control-panel"))
    R(Rule("Rooms", f"{name} too humid", "mdi:water-alert", [above(h, 75)],
           val(h, " %") + " · above 75 %", "control-panel"))

# Homelab (chips: red when a usage value is missing; HA VM view: also > 90 %).
for name, ids, extra, nav in [
        ("Proxmox host", ["sensor.proxmox_cpu_usage", "sensor.proxmox_ram_usage", "sensor.proxmox_root_disk_usage"], False, "homelab"),
        ("Ubuntu VM 100", ["sensor.cpu_usage", "sensor.ram_usage", "sensor.root_disk_usage"], False, "homelab"),
        ("HA VM 101", ["sensor.processor_use", "sensor.memory_use_percent", "sensor.disk_use_percent"], True, "server")]:
    R(Rule("Homelab", f"{name}: no data", "mdi:server-off", [is_(e, OFF) for e in ids],
           "CPU, RAM or disk usage missing", nav))
    if extra:
        R(Rule("Homelab", f"{name} above 90 %", "mdi:server", [above(e, 90) for e in ids],
               "CPU " + val(ids[0], " %") + " · RAM " + val(ids[1], " %") + " · disk " + val(ids[2], " %"), nav))
R(Rule("Homelab", "RAID md0 degraded", "mdi:database-alert",
       [above("sensor.raid_failed_disks", 0), is_("sensor.raid_failed_disks", OFF),
        below("sensor.raid_active_disks", 2), is_("sensor.raid_active_disks", OFF)],
       "{{ states('sensor.raid_active_disks') }} of 2 disks active, {{ states('sensor.raid_failed_disks') }} failed",
       "homelab-disks"))
for name, e in [("NVMe", "sensor.smart_health_nvme"), ("BigData", "sensor.smart_health_sda"),
                ("JellyMedia", "sensor.smart_health_sdd"), ("Seagate", "sensor.seagate_sde")]:
    R(Rule("Homelab", f"{name} SMART check failed", "mdi:harddisk-remove", [not_(e, "1")],
           "SMART health: " + val(e, ""), "homelab-disks"))

SERVICES = [("Bitwarden", "bitwarden"), ("browserless", "browserless"), ("cAdvisor", "cadvisor"),
            ("Grafana", "grafana"), ("HEIM", "heim"), ("Home Assistant", "home_assistant"),
            ("Jellyfin", "jellyfin"), ("Loki", "loki"), ("n8n", "n8n"), ("n8n via Funnel", "n8n_via_funnel"),
            ("Nextcloud", "nextcloud"), ("ntopng", "ntopng"), ("Paperless", "paperless"),
            ("PhotoPrism", "photoprism"), ("Prometheus", "prometheus")]
for name, k in SERVICES:
    e = f"sensor.{k}_status"
    R(Rule("Services", f"{name} down", "mdi:lan-disconnect", [is_(e, ["down"])], "Service check: down", "homelab"))
HOSTS = [("homelab (Proxmox)", [("ping", "sensor.homelab_status"), ("SSH", "sensor.homelab_ssh_status"),
                                ("node_exporter", "sensor.homelab_node_exporter_status"),
                                ("web UI", "sensor.proxmox_web_api_status"), ("exporter", "sensor.proxmox_exporter_status"),
                                ("exporter target", "sensor.proxmox_exporter_target_status"),
                                ("SMART exporter", "sensor.proxmox_smartctl_explorer_status")]),
         ("ubuntu-server", [("ping", "sensor.ubuntu_server_status"), ("SSH", "sensor.ubuntu_server_ssh_status"),
                            ("Samba", "sensor.ubuntu_server_samba_status"),
                            ("node_exporter", "sensor.ubuntu_server_node_exporter_status")]),
         ("heim-vm", [("ping", "sensor.heim_vm_status"), ("SSH", "sensor.heim_vm_ssh_status"),
                      ("node_exporter", "sensor.heim_vm_node_exporter_status")]),
         ("cp1 (k8s control plane)", [("ping", "sensor.cp1_status"), ("SSH", "sensor.cp1_ssh_status"),
                                      ("kubelet", "sensor.cp1_kubelet_status"), ("kube-proxy", "sensor.cp1_kube_proxy_status"),
                                      ("node_exporter", "sensor.cp1_node_exporter_status"),
                                      ("API server", "sensor.k8s_api_server_status")]),
         ("k8s-w1", [("ping", "sensor.k8s_w1_status"), ("SSH", "sensor.k8s_w1_ssh_status"),
                     ("kubelet", "sensor.k8s_w1_kubelet_status"), ("kube-proxy", "sensor.k8s_w1_kube_proxy_status"),
                     ("node_exporter", "sensor.k8s_w1_node_exporter_status")]),
         ("k8s-w2", [("ping", "sensor.k8s_w2_status"), ("SSH", "sensor.k8s_w2_ssh_status"),
                     ("kubelet", "sensor.k8s_w2_kubelet_status"), ("kube-proxy", "sensor.k8s_w2_kube_proxy_status"),
                     ("node_exporter", "sensor.k8s_w2_node_exporter_status")])]
for host, checks in HOSTS:
    names = ", ".join("('%s', '%s')" % (e, n) for n, e in checks)
    R(Rule("Services", f"{host}: check down", "mdi:server-network-off", [is_(e, ["down"]) for _, e in checks],
           "{%% for e, n in [%s] %%}{%% if is_state(e, 'down') %%}{{ n }} down {%% endif %%}{%% endfor %%}" % names,
           "homelab"))

# Gaming PC (chips and drive cards; nothing while the PC is off).
GP = "sensor.desktop_a498ue6_"
R(Rule("Gaming PC", "CPU above 85 °C", "mdi:cpu-64-bit",
       [above(GP + "amd_ryzen_7_5800x3d_core_tctl_tdie_temperature", 85)],
       val(GP + "amd_ryzen_7_5800x3d_core_tctl_tdie_temperature", " °C"), "gaming-pc"))
R(Rule("Gaming PC", "GPU hot spot above 100 °C", "mdi:expansion-card",
       [above(GP + "amd_radeon_rx_6750_xt_gpu_hot_spot_temperature", 100)],
       val(GP + "amd_radeon_rx_6750_xt_gpu_hot_spot_temperature", " °C"), "gaming-pc"))
R(Rule("Gaming PC", "RAM above 90 %", "mdi:memory", [above(GP + "total_memory_memory_load", 90)],
       val(GP + "total_memory_memory_load", " %"), "gaming-pc"))
# Drive cards turn red at the drive's warning temperature (81 / 84 °C now) or 60 °C.
for name, e, limit in [("Samsung 980 PRO", GP + "samsung_ssd_980_pro_1tb_composite_temperature", 80),
                       ("Crucial P3", GP + "ct1000p3pssd8_composite_temperature", 83),
                       ("Samsung 870 EVO", GP + "samsung_ssd_870_evo_500gb_temperature", 59)]:
    R(Rule("Gaming PC", f"{name} too hot", "mdi:harddisk", [above(e, limit)], val(e, " °C"), "gaming-pc"))

# Batteries at 20 % or less (Batteries view). Integer percentages: below 21.
BATTERIES = [("Bedroom sensor", "sensor.multi_sensor_bedzimmer_battery"),
             ("Babyroom sensor", "sensor.babyroom_theo_battery"),
             ("Bathroom sensor", "sensor.temperature_9_battery"),
             ("Living room sensor", "sensor.temperature_11_battery"),
             ("Vibration sensor", "sensor.vibration_6_battery"),
             ("Door sensor", "sensor.openclose_28_battery"),
             ("Iphone3G Lo-fi", "sensor.lo_fi_battery_level"),
             ("Alice's iPhone", "sensor.alice_mihalaches_iphone_2_battery_level_2")]
for name, e in BATTERIES:
    R(Rule("Batteries", f"{name} battery low", "mdi:battery-alert-variant-outline", [below(e, 21)],
           val(e, " %") + " · 20 % or less", "system-monitor"))

# Key devices that are unavailable.
OFFLINE = [("Bedroom sensor", ["sensor.multi_sensor_bedzimmer", "sensor.multi_sensor_bedzimmer_2"], "mdi:thermometer-off", "control-panel"),
           ("Theo's room sensor", ["sensor.babyroom", "sensor.babyroom_2"], "mdi:thermometer-off", "control-panel"),
           ("Bathroom sensor", ["sensor.temperature_9", "sensor.humidity_10"], "mdi:thermometer-off", "control-panel"),
           ("Living room sensor", ["sensor.living_room", "sensor.living_room_2"], "mdi:thermometer-off", "control-panel"),
           ("Air quality sensor", ["sensor.nova_pm2_5", "sensor.nova_pm10"], "mdi:air-filter", "control-panel")]
# Battery sensors of devices that have no other offline rule here.
OFFLINE += [(name, [e], "mdi:battery-off-outline", "system-monitor") for name, e in BATTERIES
            if name in ("Door sensor", "Iphone3G Lo-fi", "Alice's iPhone")]
OFFLINE += [(n, [e], "mdi:lightbulb-off-outline", "control-panel") for n, e in [
    ("Philips lamp", "light.philips_lamp"), ("Tripod lamp", "light.extended_color_light_10"),
    ("Moon lamp", "light.extended_color_light_8"), ("lidl2 lamp", "light.color_temperature_light_9"),
    ("Innr1 lamp", "light.innr1"), ("Innr2 lamp", "light.innr2")]]
OFFLINE += [(n, [e], "mdi:thermostat", "control-panel") for n, e in [
    ("Theo's thermostat", "climate.thermostat_14"), ("Living room thermostat", "climate.thermostat_15")]]
OFFLINE += [(n, [e], "mdi:power-plug-off-outline", "power") for n, e in [
    ("Homelab plug", "sensor.power_23"), ("Workstation plug", "sensor.wiz_socket_power"),
    ("Washing machine plug", "sensor.wasching_machine_plug"), ("Dishwasher plug", "sensor.power_36"),
    ("Fridge plug", "sensor.power_27"), ("TV & Audio plug", "sensor.power_30"), ("TV & Audio 2 plug", "sensor.power_32"),
    ("LiDL Outlet 1", "switch.on_off_plug_in_unit_5"), ("LiDL Outlet 2", "switch.on_off_plug_in_unit_6"),
    ("LiDL Outlet 3", "switch.on_off_plug_in_unit_7")]]
OFFLINE += [("Main door sensor", ["binary_sensor.maindoor"], "mdi:door", "control-panel"),
            ("Vibration sensor", ["binary_sensor.vibration_6"], "mdi:vibrate-off", "control-panel"),
            ("Camera hub", ["camera.camera_hub_g2hpro_d335"], "mdi:cctv-off", "control-panel")]
OFFLINE += [(f"{name}: no data", [f"sensor.{k}_status"], "mdi:lan-pending", "homelab") for name, k in SERVICES]
OFFLINE += [(f"{host}: no data", [e for _, e in checks], "mdi:server-network-off", "homelab") for host, checks in HOSTS]
for name, ids, icon, nav in OFFLINE:
    title = name if name.endswith("no data") else f"{name} offline"
    R(Rule("Offline", title, icon, [is_(e, OFF) for e in ids], "Unavailable in Home Assistant", nav))

GROUPS = [("Rooms", "mdi:home-thermometer"), ("Homelab", "mdi:server"), ("Services", "mdi:lan"),
          ("Gaming PC", "mdi:desktop-tower-monitor"), ("Batteries", "mdi:battery-alert"), ("Offline", "mdi:connection")]


# ---- Python evaluation (tests, and the check against HA before saving) ----
def _num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def atom_holds(atom, st):
    kind, e, x = atom
    s = st.get(e, "unavailable")  # a missing entity counts as unavailable
    if kind == "above":
        n = _num(s); return n is not None and n > x
    if kind == "below":
        n = _num(s); return n is not None and n < x
    if kind == "is":
        return s in x
    return s != x


def active(st):
    """Titles of the rules that are red for states {entity_id: state}."""
    return [r.title for r in RULES if any(atom_holds(a, st) for a in r.any_of)]


# ---- Jinja (same logic, rendered by Home Assistant) ----
def atom_jinja(atom):
    kind, e, x = atom
    if kind == "above":  # not a number: the default keeps it false
        return "states('%s')|float(-1e9) > %s" % (e, x)
    if kind == "below":
        return "states('%s')|float(1e9) < %s" % (e, x)
    if kind == "is":
        return "states('%s') in %s" % (e, json.dumps(x).replace('"', "'"))
    return "states('%s') != '%s'" % (e, x)


def rule_jinja(r):
    return " or ".join(atom_jinja(a) for a in r.any_of)


def jinja():
    """One line per red rule: group|title (details stay on the dashboard)."""
    return "".join("{%% if %s %%}%s|%s\n{%% endif %%}" % (rule_jinja(r), r.group, r.title) for r in RULES)


# ---- Lovelace view ----
def atom_visibility(atom):
    kind, e, x = atom
    if kind == "above":
        return {"condition": "numeric_state", "entity": e, "above": x}
    if kind == "below":
        return {"condition": "numeric_state", "entity": e, "below": x}
    if kind == "is":
        return {"condition": "state", "entity": e, "state": x}
    return {"condition": "state", "entity": e, "state_not": x}


def rule_visibility(r):
    conds = [atom_visibility(a) for a in r.any_of]
    return conds[0] if len(conds) == 1 else {"condition": "or", "conditions": conds}


def card(r):
    return {
        "type": "custom:mushroom-template-card",
        "entity": r.entity,
        "primary": r.title,
        "secondary": r.detail,
        "icon": r.icon,
        "icon_color": "red",
        "multiline_secondary": True,
        "tap_action": {"action": "navigate", "navigation_path": f"/lovelace/{r.nav}"},
        "grid_options": {"columns": 6, "rows": "auto"},
        "visibility": [rule_visibility(r)],
    }


def summary_card():
    count = "{%% set n = [%s] | select | list | count %%}" % ", ".join("(%s)" % rule_jinja(r) for r in RULES)
    return {
        "type": "custom:mushroom-template-card",
        "primary": count + "{{ 'All clear' if n == 0 else (n ~ (' alert' if n == 1 else ' alerts')) }}",
        "secondary": "Anything in the red on the dashboards shows here; tap a card to open its view",
        "icon": "mdi:alert-circle-outline",
        "icon_color": count + "{{ 'green' if n == 0 else 'red' }}",
        "multiline_secondary": True,
        "tap_action": {"action": "none"},
        "grid_options": {"columns": "full", "rows": "auto"},
    }


def view():
    sections = [{"type": "grid", "column_span": 4, "cards": [
        {"type": "heading", "heading": "Alerts", "heading_style": "title"}, summary_card()]}]
    for group, icon in GROUPS:
        rules = [r for r in RULES if r.group == group]
        any_red = {"condition": "or", "conditions": [rule_visibility(r) for r in rules]}
        sections.append({
            "type": "grid", "column_span": 2,
            "visibility": [any_red],
            "cards": [{"type": "heading", "heading": group, "icon": icon, "heading_style": "subtitle"}]
                     + [card(r) for r in rules],
        })
    return {"type": "sections", "max_columns": 4, "title": "Alerts", "path": "alerts",
            "icon": "mdi:alert-circle-outline", "sections": sections}


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "view"
    if what == "view":
        json.dump(view(), sys.stdout, indent=1, ensure_ascii=False)
    elif what == "jinja":
        sys.stdout.write(jinja())
    else:
        sys.exit("usage: alerts.py view|jinja")
