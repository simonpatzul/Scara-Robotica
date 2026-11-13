
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
