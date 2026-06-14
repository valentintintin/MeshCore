# Meshtastic ↔ MeshCore Bridge

**Coded by Human, documentation by IA and reviewed**

Bidirectional bridge enabling communication between **Meshtastic** and **MeshCore** mesh networks over serial connection.

## Features

- **Bidirectional message relay** (Meshtastic ↔ MeshCore)
- **Multi-channel support** : up to 5 configurable channels
- **Message queuing** : buffers messages with configurable delays
- **Statistics** : RX/TX counters and connection status

## Initialization

Build flags required in `platformio.ini`:

```ini
build_flags = 
  -D USE_MESHTASTIC_BRIDGE # Build with bridge code
  #-D MESHTASTIC_BRIDGE_SERIAL_PORT=Serial2 # The Serial port connected to your Meshtastic board
  #-D MESHTASTIC_BRIDGE_MAX_NODEDB=100 # The number of messageables nodes to save in RAM (to keep their name)
```

Bridge starts disabled. Configure via serial commands.

## Commands

### Global Configuration

| Command | Description | Example |
|---------|-------------|---------|
| `mt get enabled` | Bridge state | → `on` / `off` |
| `mt set enabled on\|off` | Enable/disable bridge | `mt set enabled on` |
| `mt get tx_delay` | Message send delay (ms) | → `1000 ms` |
| `mt set tx_delay N` | Set delay | `mt set tx_delay 500` |
| `mt get rx_pin` | Serial RX pin | → `16` |
| `mt set rx_pin N` | Configure RX pin | `mt set rx_pin 16` |
| `mt get tx_pin` | Serial TX pin | → `17` |
| `mt set tx_pin N` | Configure TX pin | `mt set tx_pin 17` |
| `mt get baud_rate` | Serial baud rate | → `115200 bds` |
| `mt set baud_rate N` | Set baud rate | `mt set baud_rate 115200` |
| `mt get mc_rx_timeout` | MeshCore RX timeout (s) | → `0 s` |
| `mt set mc_rx_timeout N` | Set timeout (0=disabled) | `mt set mc_rx_timeout 30` |
| `mt get mt_rx_timeout` | Meshtastic RX timeout (s) | → `0 s` |
| `mt set mt_rx_timeout N` | Set timeout (0=disabled) | `mt set mt_rx_timeout 30` |

### Channel Management

```
mt get channels              	# List all channels
mt get channel 0             	# Show channel 0 → "0: public [*]"
mt set channel 0 public *    	# Configure channel 0 (public, no region)
mt set channel 1 #le-38 fr-ara	# Configure channel 1 (channel public le-38 with region fr-ara)
mt set channel 0 -           	# Disable channel 0
```

**Format:** `mt set channel INDEX NAME [REGION]`
- `INDEX` : channel number (0-4)
- `NAME` : channel name (max 32 chars) or `-` to disable
  - `public` : uses official Meshtastic public key
  - `#custom` : custom channel with tag-based key derivation
- `REGION` : optional region code (max 31 chars)
  - `*` : global (default)
  - `custom` : custom region code

### Meshtastic Node Inspection

```
mt get node 0           # Show node 0 info → "MT node 0/5 = !abc1234 : NodeName"
```

### Diagnostics

```
mt stats                # Display RX/TX stats and bridge status
mt test                 # Send test message on primary Meshtastic channel
mt reload               # Reload preferences from file
mt save                 # Save preferences to file
mt reset                # Reset all (config, queues, stats)
```

## Message Flow

### Meshtastic → MeshCore

1. Message received on Meshtastic channel
2. Verify: channel configured + MeshCore RX active (within timeout)
3. Enqueue message with tx_delay
4. Send message with sender name prefixed with `MT_`

### MeshCore → Meshtastic

1. Group message received on configured MeshCore channel
2. Verify: channel mapped + Meshtastic RX active (within timeout)
3. Extract sender and message from Meshcore format: `[sender]: [message]`
4. Enqueue message with tx_delay
5. Send on corresponding Meshtastic channel : `MC_[sender]: [message]`

## Timeouts

- **meshcore_rx_timeout_ms** : if non-zero, blocks Meshtastic→MeshCore relay if MeshCore hasn't sent in N seconds on corresponding channel
- **meshtastic_rx_timeout_ms** : if non-zero, blocks MeshCore→Meshtastic relay if Meshtastic hasn't sent in N seconds on corresponding channel
- Set to `0` to disable timeout check (always relay)

## Channel Derivation

**Public Channel:**
- Uses base64-decoded official Meshtastic PSK
- Hash derived from PSK

**Custom Channel (#tag):**
- Secret: SHA256 of channel name
- Hash: SHA256 of secret
- Region: global (`*`) if unspecified, or region code for scoped routing

## Message Limits

- **Channel name** : max 32 characters
- **Region** : max 31 characters
- **Message text** : max 200 characters (after formatting)
- **Sender name** : max 32 characters

## Dependencies

- [Meshtastic-Arduino](https://github.com/valentintintin/Meshtastic-arduino) : my fork of the official Meshtastic Arudino library with some adjustments

## Limitations

- **Max 5 channels** per bridge
- **No private PSK** support (region code `$` not supported)
- **1:1 channel mapping** : each MeshCore channel maps to one Meshtastic channel by hash
- **Sender parsing** : assumes format `[sender]: [message]` from MeshCore
- **No multi-hop filtering** : relays all received messages regardless of hop limit
