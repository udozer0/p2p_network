# p2p_demo

Small Boost.Asio P2P networking demo.

## Build on a clean Debian/Ubuntu VPS

Install system tools:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 python3-venv
```

Install Conan in a local Python virtual environment:

```bash
python3 -m venv ~/.venvs/conan
~/.venvs/conan/bin/pip install --upgrade pip conan
```

Use Conan from that environment:

```bash
export PATH="$HOME/.venvs/conan/bin:$PATH"
conan profile detect --force
```

Install dependencies and generate CMake files:

```bash
conan install . --build=missing -s build_type=Release -s compiler.cppstd=20
```

Configure and build:

```bash
cmake --preset conan-release
cmake --build --preset conan-release
```

Run the first peer:

```bash
./build/Release/p2p 5001
```

On a VPS, it is better to publish the VPS public IP explicitly:

```bash
P2P_PUBLIC_IP=<vps_public_ip> ./build/Release/p2p 5001
```

Run another peer and connect it to the first one:

```bash
./build/Release/p2p 5002 <first_peer_ip> 5001
```

## Notes

If peers are on different networks, the listening side must be reachable from outside:

```text
TCP <listen_port> -> <machine_lan_ip>:<listen_port>
```

For example:

```text
TCP 5001 -> 192.168.1.34:5001
```

If the provider uses CG-NAT, direct incoming TCP connections will not work without a public IP, VPN, or relay server.
