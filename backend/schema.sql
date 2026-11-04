CREATE DATABASE IF NOT EXISTS jirani_circle
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE jirani_circle;

CREATE TABLE users (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  phone_e164 VARCHAR(16) CHARACTER SET ascii NOT NULL UNIQUE,
  password_hash VARCHAR(255),
  status ENUM('invited', 'active', 'disabled') NOT NULL DEFAULT 'invited',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  CONSTRAINT chk_users_password_state CHECK (
    (status = 'invited' AND password_hash IS NULL)
    OR (status IN ('active', 'disabled') AND password_hash IS NOT NULL)
  )
) ENGINE=InnoDB;

CREATE TABLE chamas (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  name VARCHAR(120) NOT NULL,
  currency CHAR(3) CHARACTER SET ascii NOT NULL DEFAULT 'KES',
  time_zone VARCHAR(64) NOT NULL DEFAULT 'Africa/Nairobi',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT chk_chamas_currency CHECK (currency = 'KES')
) ENGINE=InnoDB;

CREATE TABLE members (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
  member_code VARCHAR(32) CHARACTER SET ascii NOT NULL,
  full_name VARCHAR(120) NOT NULL,
  role ENUM('admin', 'member') NOT NULL DEFAULT 'member',
  status ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
  approval_status ENUM('not_required', 'pending', 'approved', 'denied') NOT NULL DEFAULT 'not_required',
  created_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  approved_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  approved_at DATETIME,
  joined_on DATE NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  CONSTRAINT uq_members_id_chama UNIQUE (id, chama_id),
  CONSTRAINT fk_members_chama FOREIGN KEY (chama_id) REFERENCES chamas(id),
  CONSTRAINT fk_members_user FOREIGN KEY (user_id) REFERENCES users(id),
  CONSTRAINT fk_members_creator FOREIGN KEY (created_by_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_members_approver FOREIGN KEY (approved_by_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT uq_members_chama_code UNIQUE (chama_id, member_code),
  CONSTRAINT chk_member_approval_state CHECK (
    (approval_status = 'not_required' AND approved_by_member_id IS NULL AND approved_at IS NULL)
    OR (approval_status = 'pending' AND role = 'member' AND created_by_member_id IS NOT NULL AND approved_by_member_id IS NULL AND approved_at IS NULL)
    OR (approval_status = 'approved' AND role = 'member' AND created_by_member_id IS NOT NULL AND approved_by_member_id IS NOT NULL AND approved_at IS NOT NULL AND created_by_member_id <> approved_by_member_id)
    OR (approval_status = 'denied' AND role = 'member' AND created_by_member_id IS NOT NULL AND approved_by_member_id IS NOT NULL AND approved_at IS NOT NULL AND created_by_member_id <> approved_by_member_id)
  ),
  INDEX idx_members_chama_status (chama_id, status),
  INDEX idx_members_approval_queue (chama_id, approval_status, created_at)
) ENGINE=InnoDB;

CREATE TABLE member_invitations (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  token_hash BINARY(32) NOT NULL UNIQUE,
  expires_at DATETIME NOT NULL,
  consumed_at DATETIME,
  revoked_at DATETIME,
  created_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_invitations_chama FOREIGN KEY (chama_id) REFERENCES chamas(id),
  CONSTRAINT fk_invitations_member FOREIGN KEY (member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_invitations_creator FOREIGN KEY (created_by_member_id, chama_id) REFERENCES members(id, chama_id),
  INDEX idx_invitations_member_expiry (member_id, expires_at)
) ENGINE=InnoDB;

CREATE TABLE member_monthly_targets (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  effective_from DATE NOT NULL,
  amount_minor BIGINT UNSIGNED NOT NULL,
  set_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_targets_member FOREIGN KEY (member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_targets_set_by FOREIGN KEY (set_by_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT uq_targets_member_month UNIQUE (member_id, effective_from),
  CONSTRAINT chk_targets_month_start CHECK (DAYOFMONTH(effective_from) = 1),
  CONSTRAINT chk_targets_positive CHECK (amount_minor > 0),
  INDEX idx_targets_effective (member_id, effective_from)
) ENGINE=InnoDB;

CREATE TABLE auth_sessions (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  token_hash BINARY(32) NOT NULL UNIQUE,
  expires_at DATETIME NOT NULL,
  revoked_at DATETIME,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_sessions_user FOREIGN KEY (user_id) REFERENCES users(id),
  INDEX idx_sessions_user_expiry (user_id, expires_at)
) ENGINE=InnoDB;

CREATE TABLE password_reset_tokens (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  token_hash BINARY(32) NOT NULL UNIQUE,
  expires_at DATETIME NOT NULL,
  consumed_at DATETIME,
  revoked_at DATETIME,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_password_reset_user FOREIGN KEY (user_id) REFERENCES users(id),
  INDEX idx_password_reset_user_expiry (user_id, expires_at)
) ENGINE=InnoDB;

CREATE TABLE financial_transactions (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  kind ENUM('contribution', 'disbursement', 'expense', 'adjustment') NOT NULL,
  direction ENUM('inflow', 'outflow') NOT NULL,
  status ENUM('pending', 'posted', 'failed', 'void') NOT NULL DEFAULT 'pending',
  amount_minor BIGINT UNSIGNED NOT NULL,
  payment_method ENUM('mpesa', 'bank_transfer', 'cash', 'other'),
  payment_reference VARCHAR(80) CHARACTER SET ascii,
  idempotency_key VARCHAR(128) CHARACTER SET ascii NOT NULL,
  description VARCHAR(255),
  occurred_at DATETIME,
  recorded_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  reversal_of_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin UNIQUE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  CONSTRAINT uq_transactions_id_chama UNIQUE (id, chama_id),
  CONSTRAINT fk_transactions_chama FOREIGN KEY (chama_id) REFERENCES chamas(id),
  CONSTRAINT fk_transactions_member FOREIGN KEY (member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_transactions_recorder FOREIGN KEY (recorded_by_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_transactions_reversal FOREIGN KEY (reversal_of_id, chama_id) REFERENCES financial_transactions(id, chama_id),
  CONSTRAINT uq_transactions_idempotency UNIQUE (chama_id, idempotency_key),
  CONSTRAINT chk_transactions_positive CHECK (amount_minor > 0),
  CONSTRAINT chk_contribution_member CHECK (kind <> 'contribution' OR member_id IS NOT NULL),
  CONSTRAINT chk_transaction_direction CHECK (
    kind = 'adjustment'
    OR (kind = 'contribution' AND direction = 'inflow')
    OR (kind IN ('disbursement', 'expense') AND direction = 'outflow')
  ),
  CONSTRAINT chk_posted_transaction_date CHECK (status <> 'posted' OR occurred_at IS NOT NULL),
  CONSTRAINT chk_not_own_reversal CHECK (reversal_of_id IS NULL OR reversal_of_id <> id),
  INDEX idx_transactions_chama_status_date (chama_id, status, occurred_at),
  INDEX idx_transactions_member_status_date (member_id, status, occurred_at),
  CONSTRAINT uq_transactions_payment_reference UNIQUE (chama_id, payment_method, payment_reference)
) ENGINE=InnoDB;

CREATE TABLE access_requests (
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  requester_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  target_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  records_from DATE NOT NULL,
  records_through DATE NOT NULL,
  reason VARCHAR(500) NOT NULL,
  status ENUM('pending', 'approved', 'denied') NOT NULL DEFAULT 'pending',
  requested_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  decided_by_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  decided_at DATETIME,
  consumed_at DATETIME,
  CONSTRAINT fk_access_chama FOREIGN KEY (chama_id) REFERENCES chamas(id),
  CONSTRAINT fk_access_requester FOREIGN KEY (requester_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_access_target FOREIGN KEY (target_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT fk_access_decider FOREIGN KEY (decided_by_member_id, chama_id) REFERENCES members(id, chama_id),
  CONSTRAINT chk_access_different_member CHECK (requester_member_id <> target_member_id),
  CONSTRAINT chk_access_date_range CHECK (records_from <= records_through),
  CONSTRAINT chk_access_decision_state CHECK (
    (status = 'pending' AND decided_by_member_id IS NULL AND decided_at IS NULL AND consumed_at IS NULL)
    OR (status = 'denied' AND decided_by_member_id IS NOT NULL AND decided_at IS NOT NULL AND consumed_at IS NULL)
    OR (status = 'approved' AND decided_by_member_id IS NOT NULL AND decided_at IS NOT NULL)
  ),
  CONSTRAINT chk_access_consumed_approval CHECK (consumed_at IS NULL OR status = 'approved'),
  INDEX idx_access_admin_queue (chama_id, status, requested_at),
  INDEX idx_access_requester (requester_member_id, requested_at)
) ENGINE=InnoDB;

CREATE TABLE audit_events (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  chama_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  actor_member_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  event_type VARCHAR(80) CHARACTER SET ascii NOT NULL,
  resource_type VARCHAR(80) CHARACTER SET ascii NOT NULL,
  resource_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin,
  details JSON,
  occurred_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_audit_chama FOREIGN KEY (chama_id) REFERENCES chamas(id),
  CONSTRAINT fk_audit_actor FOREIGN KEY (actor_member_id, chama_id) REFERENCES members(id, chama_id),
  INDEX idx_audit_chama_time (chama_id, occurred_at),
  INDEX idx_audit_resource (resource_type, resource_id)
) ENGINE=InnoDB;
