#pragma once

#include "lidar_interface.h"
#include "src/CYdLidar.h"
#include <memory>

class YdLidarAdapter : public LidarInterface {
public:
  YdLidarAdapter();
  virtual ~YdLidarAdapter();
  
  // LidarInterface接口实现
  bool initialize(const LidarConfig& config) override;
  bool startScan() override;
  bool stopScan() override;
  bool getScanData(LidarScanData& scan_data) override;
  void disconnect() override;
  std::string getErrorDescription() const override;
  bool isConnected() const override;
  bool isScanning() const override;
  std::string getLidarType() const override;
  bool updateConfig(const LidarConfig& config) override;

private:

  void applyConfigToLidar(const LidarConfig& config);
  

  void convertScanData(const LaserScan& ydlidar_scan, LidarScanData& scan_data);
  
  // YdLidar SDK实例
  std::unique_ptr<CYdLidar> lidar_;      
  LidarConfig current_config_;            
  bool initialized_;                     
  bool scanning_;                      
};
