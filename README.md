# Lab2 - Cliente/Servidor TCP

## 🔧 Como usar

### 1. Compilar
```bash
gcc server.c -o server -lpthread
gcc client.c -o client
```

### 2. Rodar servidor
```bash
./server
```

### 3. Conectar cliente

**Mesmo computador:**
```bash
./client localhost
```

**Computadores diferentes:**
```bash
./client [IP_DO_SERVIDOR]
```

## 📝 Comandos

### Modo interativo
```bash
./client [IP]               # Conectar
MyGet test.txt              # Ver arquivo
MyLastAccess                # Ver último acesso
quit                        # Sair
```

### Modo direto
```bash
./client [IP] test.txt              # Ver arquivo
./client [IP] test.txt local.txt    # Baixar arquivo
./client [IP] LAST                  # Ver último acesso (deve ser sempre null)
```

## 🔍 Descobrir IP
```bash
ifconfig        # Mac/Linux
ipconfig        # Windows
```