#include <gtest/gtest.h>
#include <string>
#include <ctime>

// Enum for aircraft states
enum AircraftState {
    NORMAL_CRUISE,
    LOW_FUEL_WARNING,
    CRITICAL_FUEL,
    EMERGENCY_DIVERT,
    LANDED_SAFE
};

// Packet structure
struct DataPacket {
    int aircraft_id;
    double fuel_level;
    double consumption_rate;
    double flight_time_remaining;
    std::string nearest_airport;
    bool emergency_flag;
    long timestamp;
};

// Function: calculate flight time
double calculateFlightTime(double fuel, double rate) {
    if (rate <= 0) return -1;
    return fuel / rate;
}

// Function: determine state
AircraftState determineState(double fuel) {
    if (fuel > 50) return NORMAL_CRUISE;
    if (fuel > 20) return LOW_FUEL_WARNING;
    if (fuel > 5) return CRITICAL_FUEL;
    return EMERGENCY_DIVERT;
}

// Function: validate packet
bool validatePacket(const DataPacket& packet) {
    if (packet.aircraft_id <= 0) return false;
    if (packet.fuel_level < 0) return false;
    if (packet.timestamp <= 0) return false;
    return true;
}

// Function: simulate timestamp
long getCurrentTime() {
    return std::time(nullptr);
}

// ===================== TEST CASES =====================

// ---------- Fuel Calculation Tests ----------
TEST(FuelCalculationTest, ValidCalculation) {
    double result = calculateFlightTime(100.0, 10.0);
    EXPECT_DOUBLE_EQ(result, 10.0);
}

TEST(FuelCalculationTest, ZeroConsumptionRate) {
    double result = calculateFlightTime(100.0, 0.0);
    EXPECT_EQ(result, -1);
}

TEST(FuelCalculationTest, NegativeConsumptionRate) {
    double result = calculateFlightTime(100.0, -5.0);
    EXPECT_EQ(result, -1);
}

// ---------- State Machine Tests ----------
TEST(StateMachineTest, NormalCruiseState) {
    EXPECT_EQ(determineState(80), NORMAL_CRUISE);
}

TEST(StateMachineTest, LowFuelWarningState) {
    EXPECT_EQ(determineState(30), LOW_FUEL_WARNING);
}

TEST(StateMachineTest, CriticalFuelState) {
    EXPECT_EQ(determineState(10), CRITICAL_FUEL);
}

TEST(StateMachineTest, EmergencyDivertState) {
    EXPECT_EQ(determineState(3), EMERGENCY_DIVERT);
}

// ---------- Packet Validation Tests ----------
TEST(PacketValidationTest, ValidPacket) {
    DataPacket packet = {1, 100.0, 10.0, 10.0, "YOW", false, getCurrentTime()};
    EXPECT_TRUE(validatePacket(packet));
}

TEST(PacketValidationTest, InvalidAircraftID) {
    DataPacket packet = {0, 100.0, 10.0, 10.0, "YOW", false, getCurrentTime()};
    EXPECT_FALSE(validatePacket(packet));
}

TEST(PacketValidationTest, NegativeFuelLevel) {
    DataPacket packet = {1, -10.0, 10.0, 10.0, "YOW", false, getCurrentTime()};
    EXPECT_FALSE(validatePacket(packet));
}

TEST(PacketValidationTest, MissingTimestamp) {
    DataPacket packet = {1, 100.0, 10.0, 10.0, "YOW", false, 0};
    EXPECT_FALSE(validatePacket(packet));
}

// ---------- Timestamp Tests ----------
TEST(TimestampTest, TimestampGenerated) {
    long time1 = getCurrentTime();
    EXPECT_GT(time1, 0);
}

TEST(TimestampTest, TimestampIncreases) {
    long t1 = getCurrentTime();
    long t2 = getCurrentTime();
    EXPECT_LE(t1, t2);
}

// ---------- Emergency Flag Tests ----------
TEST(EmergencyTest, EmergencyFlagTriggered) {
    DataPacket packet = {1, 3.0, 10.0, 0.3, "YOW", true, getCurrentTime()};
    EXPECT_TRUE(packet.emergency_flag);
}

TEST(EmergencyTest, NoEmergencyFlag) {
    DataPacket packet = {1, 80.0, 10.0, 8.0, "YOW", false, getCurrentTime()};
    EXPECT_FALSE(packet.emergency_flag);
}

// ===================== MAIN =====================
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}