#!/usr/bin/env python3
"""
M4G BLE Bridge Configuration Validator

This script validates the sdkconfig file for common issues and
provides recommendations for optimal performance.
"""

import sys
import re
import argparse
from pathlib import Path
from typing import List, Tuple, Optional

class ConfigIssue:
    """Represents a configuration issue or warning."""
    
    SEVERITY_ERROR = "ERROR"
    SEVERITY_WARNING = "WARNING"
    SEVERITY_INFO = "INFO"
    
    def __init__(self, severity: str, message: str, recommendation: Optional[str] = None):
        self.severity = severity
        self.message = message
        self.recommendation = recommendation
    
    def __str__(self):
        result = f"[{self.severity}] {self.message}"
        if self.recommendation:
            result += f"\n  Recommendation: {self.recommendation}"
        return result


class ConfigValidator:
    """Validates M4G BLE Bridge configuration."""
    
    def __init__(self, config_file: Path):
        self.config_file = config_file
        self.config = {}
        self.issues: List[ConfigIssue] = []
    
    def load_config(self) -> bool:
        """Load and parse the sdkconfig file."""
        if not self.config_file.exists():
            print(f"Error: Configuration file not found: {self.config_file}")
            return False
        
        with open(self.config_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                match = re.match(r'([A-Z0-9_]+)=(.*)', line)
                if match:
                    key, value = match.groups()
                    # Remove quotes from string values
                    if value.startswith('"') and value.endswith('"'):
                        value = value[1:-1]
                    # Convert y/n to boolean
                    if value == 'y':
                        value = True
                    elif value == 'n':
                        value = False
                    # Try to convert to int
                    else:
                        try:
                            value = int(value, 0)  # Handle hex with 0x prefix
                        except ValueError:
                            pass
                    
                    self.config[key] = value
        
        return True
    
    def get_config(self, key: str, default=None):
        """Get a configuration value with optional default."""
        return self.config.get(key, default)
    
    def has_config(self, key: str) -> bool:
        """Check if a configuration key exists and is enabled."""
        value = self.config.get(key)
        return value is True or value == 'y'
    
    def add_issue(self, severity: str, message: str, recommendation: Optional[str] = None):
        """Add a configuration issue."""
        self.issues.append(ConfigIssue(severity, message, recommendation))
    
    def validate_platform(self):
        """Validate platform-specific settings."""
        target = self.get_config('M4G_TARGET_ESP32S3')
        
        if target:
            # ESP32-S3 specific checks
            if not self.has_config('CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240'):
                self.add_issue(
                    ConfigIssue.SEVERITY_WARNING,
                    "ESP32-S3 CPU frequency not set to 240MHz",
                    "Set CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y for optimal USB and BLE performance"
                )
        
        nrf_target = self.get_config('M4G_TARGET_NRF52840')
        if nrf_target:
            self.add_issue(
                ConfigIssue.SEVERITY_INFO,
                "nRF52840 platform selected (experimental)",
                "This platform is under development. See docs/nrf52840_max3421e_plan.md"
            )
    
    def validate_usb_host(self):
        """Validate USB host configuration."""
        enumeration_timeout = self.get_config('M4G_USB_ENUMERATION_TIMEOUT_MS', 0)
        
        if enumeration_timeout < 1000:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"USB enumeration timeout is low ({enumeration_timeout}ms)",
                "CharaChorder dual-half keyboards may need 1000ms+ for reliable enumeration"
            )
        
        poll_period = self.get_config('M4G_USB_POLL_PERIOD_MS', 0)
        if poll_period > 100:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"USB polling period is high ({poll_period}ms)",
                "Lower values (50-100ms) provide better keystroke latency"
            )
    
    def validate_ble(self):
        """Validate BLE configuration."""
        conn_min = self.get_config('M4G_BLE_CONN_INTERVAL_MIN_MS', 0)
        conn_max = self.get_config('M4G_BLE_CONN_INTERVAL_MAX_MS', 0)
        
        if conn_min > 20:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"BLE connection interval minimum is high ({conn_min}ms)",
                "20ms is optimal for HID devices to minimize latency"
            )
        
        if conn_max < conn_min:
            self.add_issue(
                ConfigIssue.SEVERITY_ERROR,
                f"BLE connection interval max ({conn_max}ms) < min ({conn_min}ms)",
                "Maximum interval must be >= minimum interval"
            )
        
        if self.has_config('M4G_CLEAR_BONDING_ON_BOOT'):
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                "BLE bonding is cleared on every boot",
                "This should only be enabled temporarily for debugging. Disable for production."
            )
    
    def validate_power_management(self):
        """Validate power management settings."""
        sleep_timeout = self.get_config('M4G_IDLE_SLEEP_TIMEOUT_MS', 0)
        
        if sleep_timeout == 0:
            self.add_issue(
                ConfigIssue.SEVERITY_INFO,
                "Automatic sleep is disabled",
                "Enable M4G_IDLE_SLEEP_TIMEOUT_MS for battery-powered operation"
            )
        elif sleep_timeout < 30000:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"Sleep timeout is very short ({sleep_timeout}ms)",
                "Consider 60000ms (1 minute) to avoid frequent wake/sleep cycles"
            )
    
    def validate_chord_timing(self):
        """Validate CharaChorder chord detection timing."""
        chord_timeout = self.get_config('M4G_CHARACHORDER_CHORD_TIMEOUT_MS', 0)
        chord_delay = self.get_config('M4G_CHARACHORDER_CHORD_DELAY_MS', 0)
        
        if chord_timeout < 100:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"Chord timeout is very low ({chord_timeout}ms)",
                "Values <100ms may miss legitimate chord attempts. Recommended: 100-200ms"
            )
        
        if chord_timeout > 500:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"Chord timeout is high ({chord_timeout}ms)",
                "High values delay single-key passthrough. Recommended: 100-200ms"
            )
        
        if chord_delay < 10:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"Chord grace period is very low ({chord_delay}ms)",
                "CharaChorder needs time to send chord output. Recommended: 15-25ms"
            )
    
    def validate_key_repeat(self):
        """Validate key repeat settings."""
        if not self.has_config('M4G_ENABLE_KEY_REPEAT'):
            self.add_issue(
                ConfigIssue.SEVERITY_INFO,
                "Key repeat is disabled",
                "Enable M4G_ENABLE_KEY_REPEAT for standard keyboard behavior"
            )
            return
        
        repeat_delay = self.get_config('M4G_KEY_REPEAT_DELAY_MS', 0)
        repeat_rate = self.get_config('M4G_KEY_REPEAT_RATE_MS', 0)
        
        if repeat_delay < 500:
            self.add_issue(
                ConfigIssue.SEVERITY_WARNING,
                f"Key repeat delay is low ({repeat_delay}ms)",
                "May cause accidental repeats during fast typing. Recommended: 500-1000ms"
            )
        
        if repeat_rate > 50:
            self.add_issue(
                ConfigIssue.SEVERITY_INFO,
                f"Key repeat rate is slow ({repeat_rate}ms)",
                "Standard keyboards use 30-50ms. Consider 33ms for better responsiveness"
            )
    
    def validate_mouse(self):
        """Validate mouse configuration."""
        if not self.has_config('M4G_ENABLE_ARROW_MOUSE'):
            return
        
        base_speed = self.get_config('M4G_MOUSE_BASE_SPEED', 0)
        max_speed = self.get_config('M4G_MOUSE_MAX_SPEED', 0)
        
        if self.has_config('M4G_MOUSE_ENABLE_ACCELERATION'):
            if max_speed <= base_speed:
                self.add_issue(
                    ConfigIssue.SEVERITY_WARNING,
                    f"Mouse max speed ({max_speed}) <= base speed ({base_speed})",
                    "Acceleration has no effect. Increase max speed or disable acceleration."
                )
    
    def validate_memory(self):
        """Validate memory settings."""
        main_stack = self.get_config('M4G_MAIN_TASK_STACK_SIZE', 0)
        
        if main_stack < 16384:
            self.add_issue(
                ConfigIssue.SEVERITY_ERROR,
                f"Main task stack size is too small ({main_stack} bytes)",
                "USB host functionality requires at least 16384 bytes"
            )
        
        watermark_enabled = self.get_config('M4G_STACK_WATERMARK_PERIOD_MS', 0) > 0
        if not watermark_enabled:
            self.add_issue(
                ConfigIssue.SEVERITY_INFO,
                "Stack watermark logging is disabled",
                "Enable M4G_STACK_WATERMARK_PERIOD_MS to monitor memory usage during development"
            )
    
    def validate_all(self) -> int:
        """Run all validation checks."""
        if not self.load_config():
            return 1
        
        print(f"Validating configuration: {self.config_file}")
        print("=" * 70)
        
        self.validate_platform()
        self.validate_usb_host()
        self.validate_ble()
        self.validate_power_management()
        self.validate_chord_timing()
        self.validate_key_repeat()
        self.validate_mouse()
        self.validate_memory()
        
        # Print results
        errors = [i for i in self.issues if i.severity == ConfigIssue.SEVERITY_ERROR]
        warnings = [i for i in self.issues if i.severity == ConfigIssue.SEVERITY_WARNING]
        infos = [i for i in self.issues if i.severity == ConfigIssue.SEVERITY_INFO]
        
        if errors:
            print("\n❌ ERRORS:")
            for issue in errors:
                print(f"  {issue}\n")
        
        if warnings:
            print("\n⚠️  WARNINGS:")
            for issue in warnings:
                print(f"  {issue}\n")
        
        if infos:
            print("\n💡 INFO:")
            for issue in infos:
                print(f"  {issue}\n")
        
        print("=" * 70)
        print(f"Validation complete: {len(errors)} errors, {len(warnings)} warnings, {len(infos)} info")
        
        if not errors and not warnings:
            print("✅ Configuration looks good!")
            return 0
        elif errors:
            print("❌ Configuration has errors that should be fixed")
            return 1
        else:
            print("⚠️  Configuration has warnings you may want to review")
            return 0


def main():
    parser = argparse.ArgumentParser(
        description='Validate M4G BLE Bridge configuration',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                    # Validate default sdkconfig
  %(prog)s sdkconfig.qtpy     # Validate specific config file
  %(prog)s --strict           # Treat warnings as errors
        """
    )
    
    parser.add_argument(
        'config_file',
        nargs='?',
        default='sdkconfig',
        help='Configuration file to validate (default: sdkconfig)'
    )
    
    parser.add_argument(
        '--strict',
        action='store_true',
        help='Treat warnings as errors (exit code 1 if any warnings)'
    )
    
    args = parser.parse_args()
    
    config_path = Path(args.config_file)
    validator = ConfigValidator(config_path)
    
    exit_code = validator.validate_all()
    
    # In strict mode, warnings also cause failure
    if args.strict:
        warnings = [i for i in validator.issues if i.severity == ConfigIssue.SEVERITY_WARNING]
        if warnings:
            exit_code = 1
    
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
