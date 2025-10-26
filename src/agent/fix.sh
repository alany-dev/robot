#!/bin/bash

echo "=== 修复PWM权限配置 ==="

echo "当前用户: $(whoami)"
echo "用户组: $(groups)"

SUDO_PASSWORD="orangepi"


echo "设置PWM设备权限..."

for chip_dir in /sys/class/pwm/pwmchip*; do
    if [ -d "$chip_dir" ]; then
        chip_name=$(basename "$chip_dir")
        echo "处理 $chip_name..."
        
        echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/export" 2>/dev/null
        echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/unexport" 2>/dev/null
        
        echo "  释放已占用的通道0..."
        echo "$SUDO_PASSWORD" | sudo -S bash -c "echo 0 > $chip_dir/unexport" 2>/dev/null
        sleep 0.2  # 等待释放完成  

        echo "  导出通道0..."
        echo "$SUDO_PASSWORD" | sudo -S bash -c "echo 0 > $chip_dir/export" 2>/dev/null
        export_result=$?
        sleep 0.2  # 等待导出完成
        
        if [ $export_result -eq 0 ] && [ -d "$chip_dir/pwm0" ]; then
            echo "  设置通道0权限..."
            # 设置period和duty_cycle权限（最重要的控制参数）
            echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/pwm0/period" 2>/dev/null
            echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/pwm0/duty_cycle" 2>/dev/null
            echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/pwm0/enable" 2>/dev/null
            echo "$SUDO_PASSWORD" | sudo -S chmod 666 "$chip_dir/pwm0/polarity" 2>/dev/null
            echo "  通道0权限设置完成"
            
            # 验证权限设置
            if [ -w "$chip_dir/pwm0/period" ] && [ -w "$chip_dir/pwm0/duty_cycle" ]; then
                echo " 权限验证通过，可以正常使用"
            else
                echo " 权限验证失败，可能需要手动设置"
            fi
        else
            echo " 通道0导出失败"
        fi
    fi
done

echo ""
echo "权限配置完成！"
echo ""
