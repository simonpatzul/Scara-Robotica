
-- CREATE DATABASE scara;

-- Telemetria: una fila por cada scara/estado (cada 500 ms)
CREATE TABLE IF NOT EXISTS telemetria (
  id          SERIAL PRIMARY KEY,
  ts          TIMESTAMPTZ DEFAULT now(),
  ciclo       VARCHAR(12),
  pico_ok     BOOLEAN,
  temp_pico   REAL,
  temp_stm32  REAL,
  r1_pos REAL, r1_i REAL, r1_est CHAR(1), r1_falla VARCHAR(12),
  r2_pos REAL, r2_i REAL, r2_est CHAR(1), r2_falla VARCHAR(12),
  p_pos  REAL, p_i  REAL, p_est  CHAR(1), p_falla  VARCHAR(12)
);

-- Eventos: una fila por cada falla (scara/evento)
CREATE TABLE IF NOT EXISTS eventos (
  id         SERIAL PRIMARY KEY,
  ts         TIMESTAMPTZ DEFAULT now(),
  eje        VARCHAR(4),
  falla      VARCHAR(12),
  corriente  REAL
);

-- Comandos: los botones de Grafana insertan aqui; server.js los manda a scara/cmd
--   on, off, on1..on3, off1..off3, reset   -> STM32 (EN de los drivers)
--   mv r1,r2,p                             -> coordenadas objetivo (rad, rad, mm): STM32 -> Pico (I2C) -> bridge
--   seq / stop                             -> correr / parar la secuencia de la caja (la corre el PC de la Pico)
CREATE TABLE IF NOT EXISTS comandos (
  id       SERIAL PRIMARY KEY,
  ts       TIMESTAMPTZ DEFAULT now(),
  cmd      VARCHAR(40) NOT NULL,
  enviado  BOOLEAN DEFAULT false
);

-- Aviso inmediato a server.js (LISTEN scara_cmd) en cada comando nuevo
CREATE OR REPLACE FUNCTION avisar_comando() RETURNS trigger AS $$
BEGIN
  PERFORM pg_notify('scara_cmd', NEW.id::text);
  RETURN NEW;
END $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS comandos_aviso ON comandos;
CREATE TRIGGER comandos_aviso AFTER INSERT ON comandos
  FOR EACH ROW EXECUTE FUNCTION avisar_comando();
