-- schema.sql
-- Esquema de persistencia para api-client-github (ver README.md, sección "Persistencia").
-- CREATE TABLE IF NOT EXISTS es idempotente: se puede ejecutar en cada arranque
-- de la app sin riesgo de pisar datos existentes.

CREATE TABLE IF NOT EXISTS assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_uri TEXT UNIQUE NOT NULL,
    title TEXT,
    entity TEXT NOT NULL,
    provider TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    meta_payload TEXT
);

