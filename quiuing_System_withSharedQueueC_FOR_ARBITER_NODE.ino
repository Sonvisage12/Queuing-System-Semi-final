#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <WiFi.h>
#include <map>
#include <vector>
#include <algorithm>
#include "SharedQueue.h"
#include <Wire.h>
#include "RTClib.h"
#define IR_SENSOR_PIN 0  // GPIO 0
#include <ESP32Servo.h>
#define SERVO_PIN 26
Servo myServo;
bool isOpen = false;
#define RST_PIN  5
#define SS_PIN   4
#define GREEN_LED_PIN 15
#define RED_LED_PIN   2
#define BLUE_LED_PIN   0
#define BUZZER   27
MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences prefs;

SharedQueue sharedQueue("rfid-patients");
SharedQueue sharedQueueA("queue-A");
SharedQueue sharedQueueB("queue-B");
SharedQueue sharedQueueC("queue-C");

RTC_DS3231 rtc;
bool isArrivalNode = false, isDoctorNode = false;
std::map<String, unsigned long> recentUIDs;
const unsigned long UID_CACHE_TTL_MS = 2000;
const uint8_t arrivalMACs[][6] = {
    
    {0x78, 0x1C, 0x3C, 0x2D, 0xA2, 0xA4},
    {0x00, 0x4B, 0x12, 0x97, 0x2E, 0xA4},  //78:1C:3C:2D:A2:A4
    {0x5C, 0x01, 0x3B, 0x97, 0x54, 0xB4}, //00:4B:12:97:2E:A4
    {0x78, 0x1C, 0x3C, 0xE6, 0x6C, 0xB8}, //78:1C:3C:E6:6C:B8
    {0x78, 0x1C, 0x3C, 0xE3, 0xAB, 0x30}, //78:1C:3C:E3:AB:30
    {0x5C, 0x01, 0x3B, 0x98, 0xDB, 0x04}, //5C:01:3B:98:DB:04
    {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C} //78:42:1C:6C:E4:9C
};
const int numArrivalNodes = sizeof(arrivalMACs) / sizeof(arrivalMACs[0]);

const uint8_t doctorMACs[][6] = {
    {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C},
    {0x5C, 0x01, 0x3B, 0x98, 0x3C, 0xEC},
    {0x6C, 0xC8, 0x40, 0x06, 0x2C, 0x8C},
     {0x78, 0x1C, 0x3C, 0xE5, 0x50, 0x0C}
};
void broadcastToArrivalNodes(const QueueItem &item) {
    for (int i = 0; i < numArrivalNodes; i++) {
        esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
    }
}

void cleanupRecentUIDs() {
    unsigned long now = millis();
    for (auto it = recentUIDs.begin(); it != recentUIDs.end(); ) {
        if (now - it->second > UID_CACHE_TTL_MS)
            it = recentUIDs.erase(it);
        else
            ++it;
    }
}

void handleQueuePlacement(String uid, String timestamp, int number) {
    if (sharedQueueA.exists(uid)) {
        sharedQueueA.removeByUID(uid);
        sharedQueueB.add(uid, timestamp, number);
        Serial.println("🔄 UID moved from SharedQueueA to SharedQueueB.");
    } else if (sharedQueue.exists(uid) || sharedQueueB.exists(uid)) {
        Serial.println("⚠️ UID already in a queue. Skipping addition.");
    } else {
        sharedQueue.add(uid, timestamp, number);
        Serial.println("✅ UID added to SharedQueue.");
    }

    sharedQueue.print(); sharedQueueA.print(); sharedQueueB.print(); sharedQueueC.print();
}
void onDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));
    const uint8_t* mac = recvInfo->src_addr;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("📩 Received from: "); Serial.println(macStr);

    isArrivalNode = isDoctorNode = false;
    for (int i = 0; i < numArrivalNodes; i++)
        if (memcmp(mac, arrivalMACs[i], 6) == 0) { isArrivalNode = true; break; }
    for (int i = 0; i < 4; i++)
        if (memcmp(mac, doctorMACs[i], 6) == 0) { isDoctorNode = true; break; }

        if (isArrivalNode) { Serial.println("🔄 Handling Arrival Node message...");
                        //     if (item.addToQueue && !item.removeFromQueue) {
                        //     String uidStr = String(item.uid);
                        //     String timestampStr = String(item.timestamp);
                        //     int number = item.number;

                        //     if (sharedQueueA.exists(uidStr)) {
                        //         sharedQueueA.removeByUID(uidStr);
                        //         sharedQueueB.add(uidStr, timestampStr, number);
                        //         Serial.println("🔄 UID moved from SharedQueueA to SharedQueueB.");
                        //     } else if (sharedQueue.exists(uidStr) || sharedQueueB.exists(uidStr)) {
                        //         Serial.println("⚠️ UID already in a queue. Skipping addition.");
                        //     } else {
                        //         sharedQueue.add(uidStr, timestampStr, number);
                        //         Serial.println("✅ UID added to SharedQueue.");
                        //     }
                        // createMixedQueue(); 
                        //     sharedQueue.print(); sharedQueueA.print(); sharedQueueB.print(); sharedQueueC.print();
                        // }

    Serial.println("🔄 Handling Arrival Node message...");
    String uidStr = String(item.uid);
    cleanupRecentUIDs();

    if (item.addToQueue && !item.removeFromQueue) {
        if (recentUIDs.count("add:" + uidStr)) {
            Serial.println("⏳ UID recently added, skipping to prevent rebroadcast loop.");
        } else {
            recentUIDs["add:" + uidStr] = millis();
            handleQueuePlacement(uidStr, String(item.timestamp), item.number);
        }
    } else if (!item.addToQueue && item.removeFromQueue) {
        if (recentUIDs.count("rem:" + uidStr)) {
            Serial.println("⏳ UID recently removed, skipping to prevent rebroadcast loop.");
        } else {
            recentUIDs["rem:" + uidStr] = millis();

            if (sharedQueue.exists(uidStr)) {
                sharedQueue.removeByUID(uidStr);
                sharedQueueC.removeByUID(uidStr);
                sharedQueueA.add(uidStr, String(item.timestamp), item.number);
                Serial.printf("🗑️ UID %s removed from SharedQueue and added to SharedQueueA.\n", item.uid);
            } else {
                sharedQueueC.removeByUID(uidStr);
                sharedQueueB.removeByUID(uidStr);
                Serial.printf("🗑️ UID %s removed from SharedQueueC and SharedQueueB.\n", item.uid);
            }
        }
       
        sharedQueue.print(); sharedQueueA.print(); sharedQueueB.print(); sharedQueueC.print();
    }



//  else if (!item.addToQueue && item.removeFromQueue) {
//             if (sharedQueue.exists(String(item.uid))) {
//                 sharedQueue.removeByUID(String(item.uid));
//                 sharedQueueC.removeByUID(String(item.uid));
//                 sharedQueueA.add(String(item.uid), String(item.timestamp), item.number);
//                 Serial.printf("🗑️ UID %s removed from SharedQueue and added to SharedQueueA.\n", item.uid);
//             } else {
//                 sharedQueueC.removeByUID(String(item.uid));
//                 sharedQueueB.removeByUID(String(item.uid));
//                 Serial.printf("🗑️ UID %s removed from SharedQueueC and SharedQueueB.\n", item.uid);
//             }
//             Serial.printf("🔁 Synchronized removal of UID: %s\n", item.uid); createMixedQueue(); 
//              sharedQueue.print();sharedQueueA.print();sharedQueueB.print();sharedQueueC.print();
//         }
    } else if (isDoctorNode) {
         Serial.println("👨‍⚕️ Doctor Node message received...");
        if (strcmp(item.uid, "REQ_NEXT") == 0) {
             Serial.println("📬 Handling 'REQ_NEXT' from Doctor...");
            int doctorNodeID = -1;
            for (int i = 0; i < 4; i++){
                if (memcmp(mac, doctorMACs[i], 6) == 0) {
                doctorNodeID = i + 1;
                break;
                }
            }
               sharedQueue.print();sharedQueueA.print();sharedQueueB.print();sharedQueueC.print();
            if (doctorNodeID == -1){
                 Serial.println("❌ Unknown Doctor Node MAC!");
                  return;}
            if (!sharedQueueC.empty()) {
    QueueEntry entry = sharedQueueC.front();
    QueueItem sendItem;
    strncpy(sendItem.uid, entry.uid.c_str(), sizeof(sendItem.uid));
    strncpy(sendItem.timestamp, entry.timestamp.c_str(), sizeof(sendItem.timestamp));
    sendItem.number = entry.number;
    sendItem.node = doctorNodeID;
    sendItem.addToQueue = false;
    sendItem.removeFromQueue = false;

    // Send to doctor
    esp_now_send(mac, (uint8_t*)&sendItem, sizeof(sendItem));

    // Remove from MixedQueue
    // Move to bottom of the correct original queue
    if (sharedQueue.exists(entry.uid)) {
       // sharedQueue.removeByUID(entry.uid); 
        sharedQueue.pop();      // Remove original position
        sharedQueue.push(entry); 
         sharedQueueC.pop(); sharedQueueC.push(entry);           // Push to back
    } else if (sharedQueueB.exists(entry.uid)) {
        sharedQueueB.pop(); sharedQueueC.pop();
        sharedQueueB.push(entry);sharedQueueC.push(entry);
    } 
                QueueItem updateItem = sendItem;
                updateItem.addToQueue = true;
                updateItem.removeFromQueue = false;
                broadcastToArrivalNodes(updateItem);
                Serial.printf("🛱 Broadcasted update to Arrival Nodes: UID %s\n", updateItem.uid);
           
}
 else {
                QueueItem zeroItem = {};
                strncpy(zeroItem.uid, "NO_PATIENT", sizeof(zeroItem.uid));
                zeroItem.number = 0;
                zeroItem.node = doctorNodeID;
                zeroItem.addToQueue = false;
                zeroItem.removeFromQueue = false;
                esp_now_send(mac, (uint8_t*)&zeroItem, sizeof(zeroItem));
                Serial.printf("⚠️ Queue empty. Sent 'NO_PATIENT' to Doctor Node %d.\n", doctorNodeID);
          
            }
        } else if (item.removeFromQueue) {
            item.addToQueue = false;
            item.removeFromQueue = true;
            for (int i = 0; i < numArrivalNodes; i++) {
                if (memcmp(arrivalMACs[i], WiFi.macAddress().c_str(), 6) != 0)
                    esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
            }
            Serial.println("📤 Broadcasted removal to Arrival Nodes.");

            if (sharedQueue.exists(String(item.uid))) {
                sharedQueue.removeByUID(String(item.uid));
                sharedQueueC.removeByUID(String(item.uid));
                sharedQueueA.add(String(item.uid), String(item.timestamp), item.number);
                Serial.printf("🗑️ UID %s removed from SharedQueue and added to SharedQueueA.\n", item.uid);
            } else {
                sharedQueueC.removeByUID(String(item.uid));
                sharedQueueB.removeByUID(String(item.uid));
                Serial.printf("🗑️ UID %s removed from SharedQueueC and SharedQueueB.\n", item.uid);
            }
           broadcastToArrivalNodes(item);
           
            if (sharedQueueC.empty()) {
                Serial.println("⚠️ Queue is now empty. Sending NO_PATIENT to all Doctor Nodes...");
                for (int i = 0; i < 4; i++) {
                    QueueItem zeroItem = {};
                    strncpy(zeroItem.uid, "NO_PATIENT", sizeof(zeroItem.uid));
                    zeroItem.number = 0;
                    zeroItem.node = i + 1;
                    zeroItem.addToQueue = false;
                    zeroItem.removeFromQueue = false;
                    esp_now_send(doctorMACs[i], (uint8_t*)&zeroItem, sizeof(zeroItem));
                }
                Serial.println("📤 Sent NO_PATIENT to all doctors.");
            }
        }
    }
    // createMixedQueue(); 

}






int getPermanentNumber(String uid) {
    prefs.begin("rfidMap", true); int pid=-1;
    if (prefs.isKey(uid.c_str())) pid = prefs.getUInt(uid.c_str(),-1);
    prefs.end(); return pid;
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent 🟢" : "Failed 🔴");
}

void processCard(String uid) {
    DateTime now = rtc.now();
    char timeBuffer[25];
    snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    String timeStr = String(timeBuffer);

    int pid = getPermanentNumber(uid);
    if (pid == -1) pid = 0;

    QueueItem item;
    strncpy(item.uid, uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, timeStr.c_str(), sizeof(item.timestamp));
    item.number = pid;
    item.addToQueue = true; item.removeFromQueue = false;

    if (sharedQueueA.exists(uid)) {
        sharedQueueA.removeByUID(uid);
        sharedQueueB.add(uid, timeStr, pid);
        for (int i = 0; i < numArrivalNodes; i++) esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
        Serial.println("🔄 UID moved from SharedQueueA to SharedQueueB.");
        blinkLED(GREEN_LED_PIN);  // Success indicator digitalWrite(GREEN_LED_PIN, LOW );
    } else if (sharedQueue.exists(uid) || sharedQueueB.exists(uid)) {
        Serial.println("⚠️ UID already in queue.");
        blinkLED(RED_LED_PIN);  // Warning indicator
    } else {
        sharedQueue.add(uid, timeStr, pid);
        for (int i = 0; i < numArrivalNodes; i++) esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
        Serial.println("✅ UID added to SharedQueue.");
        blinkLED(GREEN_LED_PIN);  // Success indicator
    }
    sharedQueue.print();sharedQueueA.print();sharedQueueB.print();sharedQueueC.print();
}

// void createMixedQueue() {
//     sharedQueueC.clear();
//     std::vector<QueueEntry> entriesA = sharedQueue.getAll();
//     std::vector<QueueEntry> entriesB = sharedQueueB.getAll();
//     size_t indexA = 0, indexB = 0;
//     while (indexA < entriesA.size() || indexB < entriesB.size()) {
//         for (int i = 0; i < 5 && indexA < entriesA.size(); i++, indexA++)
//             sharedQueueC.add(entriesA[indexA].uid, entriesA[indexA].timestamp, entriesA[indexA].number);
//         for (int i = 0; i < 3 && indexB < entriesB.size(); i++, indexB++)
//             sharedQueueC.add(entriesB[indexB].uid, entriesB[indexB].timestamp, entriesB[indexB].number);
//     }
// }

// void createMixedQueue() {
//     sharedQueueC.clear();

//     std::vector<QueueEntry> entriesA = sharedQueue.getAll();
//     std::vector<QueueEntry> entriesB = sharedQueueB.getAll();
//     size_t indexA = 0, indexB = 0;

//     while (indexA < entriesA.size() || indexB < entriesB.size()) {
//         // Priority: serve from B first if not empty
//         if (indexB < entriesB.size()) {
//             for (int i = 0; i < 10 && indexB < entriesB.size(); i++, indexB++) {
//                 sharedQueueC.add1(entriesB[indexB].uid, entriesB[indexB].timestamp, entriesB[indexB].number);
//             }
//         }

//         // Then serve from A
//         if (indexA < entriesA.size()) {
//             for (int i = 0; i < 5 && indexA < entriesA.size(); i++, indexA++) {
//                 sharedQueueC.add1(entriesA[indexA].uid, entriesA[indexA].timestamp, entriesA[indexA].number);
//             }
//         }
//     }
// }

void createMixedQueue() {
    sharedQueueC.clear();

    std::vector<QueueEntry> entriesA = sharedQueue.getAll();
    std::vector<QueueEntry> entriesB = sharedQueueB.getAll();
    size_t indexA = 0, indexB = 0;

    while (indexA < entriesA.size() || indexB < entriesB.size()) {
        if (indexB < entriesB.size()) {
            for (int i = 0; i < 10 && indexB < entriesB.size(); i++, indexB++) {
                sharedQueueC.add1(entriesB[indexB].uid, entriesB[indexB].timestamp, entriesB[indexB].number);
            }
        }

        if (indexA < entriesA.size()) {
            for (int i = 0; i < 5 && indexA < entriesA.size(); i++, indexA++) {
                sharedQueueC.add1(entriesA[indexA].uid, entriesA[indexA].timestamp, entriesA[indexA].number);
            }
        }
    }

    // Broadcast updated sharedQueueC entries to all Arrival Nodes
    std::vector<QueueEntry> entriesC = sharedQueueC.getAll();
    for (const auto& entry : entriesC) {
        QueueItem item;
        strncpy(item.uid, entry.uid.c_str(), sizeof(item.uid));
        strncpy(item.timestamp, entry.timestamp.c_str(), sizeof(item.timestamp));
        item.number = entry.number;
        item.addToQueue = true;
        item.removeFromQueue = false;
        broadcastToArrivalNodes(item);
    }
    Serial.println("📡 Broadcasted sharedQueueC to all Arrival Nodes.");
}


void printAllQueues() {
    Serial.println("📋 All Queues:");
    Serial.print("🔸 sharedQueue: "); sharedQueue.print();
    Serial.print("🔸 sharedQueueA: "); sharedQueueA.print();
    Serial.print("🔸 sharedQueueB: "); sharedQueueB.print();
    Serial.print("🔸 sharedQueueC: "); sharedQueueC.print();
}

void clearAllQueues() {
    sharedQueue.clear(); sharedQueueA.clear(); sharedQueueB.clear(); sharedQueueC.clear();
    Serial.println("🔄 All queues cleared.");
}


 void setup() {
    Serial.begin(115200);
    prefs.begin("rfidMap", false);
     pinMode(IR_SENSOR_PIN, INPUT);
  myServo.attach(SERVO_PIN, 500, 2500);  // Correct for ESP32 + SG90
  myServo.write(0);  // Start at 0 degrees
clearAllQueues();
    // Hardcoded RFID UID to patient number mappings
    // prefs.putUInt("13B6B1E3", 1);  prefs.putUInt("13D7ADE3", 2);  prefs.putUInt("A339D9E3", 3);
    // prefs.putUInt("220C1805", 4);  prefs.putUInt("638348E3", 5);  prefs.putUInt("A3E9C7E3", 6);
    // prefs.putUInt("5373BEE3", 7);  prefs.putUInt("62EDFF51", 8);  prefs.putUInt("131DABE3", 9);
    // prefs.putUInt("B3D4B0E3", 10); prefs.putUInt("23805EE3", 11); prefs.putUInt("1310BAE3", 12);
    // prefs.putUInt("D38A47E3", 13); prefs.putUInt("6307D8E3", 14); prefs.putUInt("D35FC4E3", 15);
    // prefs.putUInt("C394B9E3", 16); prefs.putUInt("53D8E1E3", 17); prefs.putUInt("93DAE1E3", 18);
    // prefs.putUInt("7367ABE3", 19); prefs.putUInt("7304BFE3", 20); prefs.putUInt("D374B6E3", 21);
    // prefs.putUInt("33A7BDE3", 22); prefs.putUInt("9308C2E3", 23); prefs.putUInt("83CDC8E3", 24);
    // prefs.putUInt("3337ECE3", 25); prefs.putUInt("C37ABFE3", 26); prefs.putUInt("D392ADE3", 27);
    // prefs.putUInt("336AB9E3", 28); prefs.putUInt("B3B7AFE3", 29); prefs.putUInt("E396CBE3", 30);
 prefs.putUInt("046EF5A54F6181", 1);
 prefs.putUInt("046E83A34F6181", 2);
 prefs.putUInt("04D2BFA34F6180", 3);
 prefs.putUInt("04F12AA34F6180", 4);
 prefs.putUInt("04D0C5A64F6180", 5);
 prefs.putUInt("043EC9A34F6180", 6);
 prefs.putUInt("04B53DA64F6180", 7);
 prefs.putUInt("04F594A34F6180", 8);
 prefs.putUInt("04C459A64F6180", 9);
 prefs.putUInt("04E0D1A34F6180", 10);
 prefs.putUInt("04962BA34F6180", 11);
 prefs.putUInt("04E1FCA34F6180", 12);
 prefs.putUInt("040431A64F6180", 13);
 prefs.putUInt("044993A34F6180", 14);
 prefs.putUInt("0431EAA34F6180", 15);
 prefs.putUInt("046035A34F6180", 16);
 prefs.putUInt("04E6C4A34F6180", 17);
 prefs.putUInt("04A015A34F6180", 18);
 prefs.putUInt("042E4BA34F6181", 19);
 prefs.putUInt("0493F1A34F6181", 20);
 prefs.putUInt("040DBBA34F6180", 21);
 prefs.putUInt("04AE01A34F6181", 22);
 prefs.putUInt("0429E5A34F6180", 23);
 prefs.putUInt("0436EAA34F6180", 24);
 prefs.putUInt("048E9FA34F6180", 25);
 prefs.putUInt("0491C9A34F6180", 26);
 prefs.putUInt("041673A34F6180", 27);
 prefs.putUInt("0447EB283F6180", 28);
 prefs.putUInt("044DBFA34F6180", 29);
 prefs.putUInt("04A171A34F6180", 30);
 prefs.putUInt("04300BA44F6180", 31);
 prefs.putUInt("046303A44F6180", 32);
 prefs.putUInt("048787A64F6180", 33);
 prefs.putUInt("044279A34F6180", 34);
 prefs.putUInt("04914FA34F6180", 35);
 prefs.putUInt("04E52BA34F6181", 36);
 prefs.putUInt("04115DA34F6180", 37);
 prefs.putUInt("0462DAA54F6180", 38);
 prefs.putUInt("043CF1A34F6180", 39);
 prefs.putUInt("04F5A8A34F6180", 40);
 prefs.putUInt("04BB7CC36F6180", 41);
 prefs.putUInt("049B5CA34F6180", 42);
 prefs.putUInt("04D65EC16F6180", 43);
 prefs.putUInt("04330DA34F6180", 44);
 prefs.putUInt("04035BA34F6180", 45);
 prefs.putUInt("04FBC9A34F6180", 46);
 prefs.putUInt("049BF5A34F6180", 47);
 prefs.putUInt("04BBE0A34F6180", 48);
 prefs.putUInt("04DB0BA34F6180", 49);
 prefs.putUInt("04C666A34F6180", 50);
 prefs.putUInt("045A71A34F6180", 51);
 prefs.putUInt("047CB3A54F6180", 52);
 prefs.putUInt("04F76DA34F6181", 53);
 prefs.putUInt("041B46A34F6180", 54);
 prefs.putUInt("048572A64F6180", 55);
 prefs.putUInt("0469A9A34F6180", 56);
 prefs.putUInt("040D2AA34F6180", 57);
 prefs.putUInt("049472A34F6180", 58);
 prefs.putUInt("04E203A44F6180", 59);
 prefs.putUInt("041D3AA34F6180", 60);
 prefs.putUInt("046362A34F6180", 61);
 prefs.putUInt("046E20A34F6181", 62);
 prefs.putUInt("047683C36F6180", 63);
 prefs.putUInt("047E11A34F6180", 64);
 prefs.putUInt("04F6D7A34F6180", 65);
 prefs.putUInt("048D24A34F6180", 66);
 prefs.putUInt("04F6E0A34F6180", 67);
 prefs.putUInt("043103A44F6180", 68);
 prefs.putUInt("043AFDA24F6180", 69);
 prefs.putUInt("044795A34F6180", 70);
 prefs.putUInt("04A33FA34F6180", 71);
 prefs.putUInt("043ECDA34F6180", 72);
 prefs.putUInt("041A57A34F6180", 73);
 prefs.putUInt("04F32FA34F6180", 74);
 prefs.putUInt("0440F0A24F6180", 75);
 prefs.putUInt("04FDCEA34F6180", 76);
 prefs.putUInt("0429D1A24F6180", 77);
 prefs.putUInt("0431AAA34F6180", 78);
 prefs.putUInt("04D1F9A24F6180", 79);
 prefs.putUInt("04E0F0A24F6180", 80);
 prefs.putUInt("04E7F0A34F6180", 81);
 prefs.putUInt("048D9FA34F6180", 82);
 prefs.putUInt("04D135A34F6180", 83);
 prefs.putUInt("0472D7A34F6180", 84);
 prefs.putUInt("0455E0A34F6180", 85);
 prefs.putUInt("04BEF5A34F6180", 86);
 prefs.putUInt("046BB9A34F6180", 87);
 prefs.putUInt("045BE7A24F6180", 88);
 prefs.putUInt("0429CEA64F6180", 89);
 prefs.putUInt("04B6A9A34F6180", 90);
 prefs.putUInt("04CBF9A24F6180", 91);
 prefs.putUInt("04569FA34F6180", 92);
 prefs.putUInt("046BFDA34F6180", 93);
 prefs.putUInt("04B5FCA34F6180", 94);
 prefs.putUInt("044CFEA24F6180", 95);
 prefs.putUInt("042B40A34F6180", 96);
 prefs.putUInt("046CC8A54F6180", 97);
 prefs.putUInt("045E9FA34F6180", 98);
 prefs.putUInt("04E392A34F6180", 99);
 prefs.putUInt("04EB86C36F6180", 100);
 prefs.putUInt("0459C5A54F6180", 101);
 prefs.putUInt("04D1D1A34F6180", 102);
 prefs.putUInt("04D7D6A34F6180", 103);
 prefs.putUInt("04851EA14F6180", 104);
 prefs.putUInt("048624A34F6180", 105);
 prefs.putUInt("0464B5A34F6180", 106);
 prefs.putUInt("04B6D6A34F6180", 107);
 prefs.putUInt("04B9C3A54F6180", 108);
 prefs.putUInt("04C63AA34F6180", 109);
 prefs.putUInt("042094A34F6180", 110);
 prefs.putUInt("04BA5CA34F6180", 111);
 prefs.putUInt("04DAC8A34F6180", 112);
 prefs.putUInt("0461DBA04F6180", 113);
 prefs.putUInt("0496C2A64F6180", 114);
 prefs.putUInt("04AED6A34F6181", 115);
 prefs.putUInt("048C26A34F6180", 116);
 prefs.putUInt("043604A44F6180", 117);
 prefs.putUInt("04C5F8A34F6180", 118);
 prefs.putUInt("04EC98A34F6180", 119);
 prefs.putUInt("041EE9A34F6180", 120);
 prefs.putUInt("045DE1A34F6180", 121);
 prefs.putUInt("047B9AA34F6180", 122);
 prefs.putUInt("0413E4A34F6181", 123);
 prefs.putUInt("04C2E7A24F6180", 124);
 prefs.putUInt("0401F5A24F6181", 125);
 prefs.putUInt("0416D1A24F6180", 126);
 prefs.putUInt("04FADAA54F6180", 127);
 prefs.putUInt("040EBBA34F6180", 128);
 prefs.putUInt("043910A34F6180", 129);
 prefs.putUInt("044BD2A34F6180", 130);
 prefs.putUInt("0466FCA34F6180", 131);
 prefs.putUInt("04390BA44F6180", 132);
 prefs.putUInt("0484E4A24F6180", 133);
 prefs.putUInt("04A2CDA34F6180", 134);
 prefs.putUInt("040ED6C26F6180", 135);
 prefs.putUInt("04E0F0A34F6180", 136);
 prefs.putUInt("042B88A34F6180", 137);
 prefs.putUInt("048AE4A24F6181", 138);
 prefs.putUInt("044D3BA34F6180", 139);
 prefs.putUInt("04BA61A34F6180", 140);
 prefs.putUInt("0420E9A34F6180", 141);
 prefs.putUInt("04CE1AA34F6180", 142);
 prefs.putUInt("04BEFDA34F6180", 143);
 prefs.putUInt("04B5DFA34F6180", 144);
 prefs.putUInt("0462A0A34F6180", 145);
 prefs.putUInt("043975C16F6180", 146);
 prefs.putUInt("04FDEEA24F6180", 147);
 prefs.putUInt("044CECA24F6180", 148);
 prefs.putUInt("045501A34F6180", 149);
 prefs.putUInt("0449E4A24F6180", 150);
 prefs.putUInt("04FE9EA34F6180", 151);
 prefs.putUInt("042AD3A24F6180", 152);
 prefs.putUInt("047CEDA34F6180", 153);
 prefs.putUInt("04AEFDA34F6181", 154);
 prefs.putUInt("043560A34F6180", 155);
 prefs.putUInt("04263BA34F6180", 156);
 prefs.putUInt("044511A34F6180", 157);
 prefs.putUInt("04EAA8A34F6180", 158);
 prefs.putUInt("04EA35A34F6180", 159);
 prefs.putUInt("04943AA34F6180", 160);
 prefs.putUInt("042CD3A24F6180", 161);
 prefs.putUInt("04EA86C36F6180", 162);
 prefs.putUInt("049ACAA04F6180", 163);
 prefs.putUInt("0459D2A34F6180", 164);
 prefs.putUInt("043125A34F6180", 165);
 prefs.putUInt("0407C3A34F6180", 166);
 prefs.putUInt("040206A34F6180", 167);
 prefs.putUInt("04009BA54F6180", 168);
 prefs.putUInt("04851BA34F6180", 169);
 prefs.putUInt("044477A34F6180", 170);
 prefs.putUInt("0469BAA34F6180", 171);
 prefs.putUInt("045E1BA34F6180", 172);
 prefs.putUInt("0434E9A34F6180", 173);
 prefs.putUInt("04F42AA34F6180", 174);
 prefs.putUInt("04EB93A34F6180", 175);
 prefs.putUInt("043DD3A24F6180", 176);
 prefs.putUInt("04B2DFA34F6180", 177);
 prefs.putUInt("04F3B8A34F6180", 178);
 prefs.putUInt("04210BA34F6180", 179);
 prefs.putUInt("04A67BA64F6180", 180);
 prefs.putUInt("046EB9A34F6181", 181);
 prefs.putUInt("04E1D1A34F6180", 182);
 prefs.putUInt("04C372A34F6180", 183);
 prefs.putUInt("044BC9A34F6180", 184);
 prefs.putUInt("04AC50A34F6180", 185);
 prefs.putUInt("049361A34F6181", 186);
 prefs.putUInt("042B4BA34F6180", 187);
 prefs.putUInt("04EC0AA34F6180", 188);
 prefs.putUInt("0464F4A24F6180", 189);
 prefs.putUInt("044E95A34F6180", 190);
 prefs.putUInt("04D999A34F6180", 191);
 prefs.putUInt("04BA71A34F6180", 192);
 prefs.putUInt("044609A44F6180", 193);
 prefs.putUInt("0496B9A34F6180", 194);
 prefs.putUInt("0449A4A34F6180", 195);
 prefs.putUInt("04D6A6A64F6180", 196);
 prefs.putUInt("04AEEFA24F6181", 197);
 prefs.putUInt("044C20A34F6180", 198);
 prefs.putUInt("0440CEA34F6180", 199);
 prefs.putUInt("0440D3A24F6180", 200);
 prefs.putUInt("04EB56A34F6180", 201);
 prefs.putUInt("04510DA44F6180", 202);
 prefs.putUInt("041D0CA34F6180", 203);
 prefs.putUInt("049A09A44F6180", 204);
 prefs.putUInt("04DE37A64F6180", 205);
 prefs.putUInt("0451D2A34F6180", 206);
 prefs.putUInt("04E1B4A34F6180", 207);
 prefs.putUInt("04C5E8A34F6180", 208);
 prefs.putUInt("042CF9A24F6180", 209);
 prefs.putUInt("043388A34F6180", 210);
 prefs.putUInt("04FCF8A24F6180", 211);
 prefs.putUInt("04F4DBA34F6180", 212);
 prefs.putUInt("04C3AFA34F6180", 213);
 prefs.putUInt("046D7EA34F6180", 214);
 prefs.putUInt("04BB8FA34F6180", 215);
 prefs.putUInt("04D459A64F6180", 216);
 prefs.putUInt("043C49A34F6180", 217);
 prefs.putUInt("040E5BA34F6180", 218);
 prefs.putUInt("047ECDA34F6180", 219);
 prefs.putUInt("04500DA44F6180", 220);
 prefs.putUInt("048C46A34F6180", 221);
 prefs.putUInt("04D30BA34F6181", 222);
 prefs.putUInt("04302FA34F6180", 223);
 prefs.putUInt("04A7EDA34F6180", 224);
 prefs.putUInt("04B3E8A44F6180", 225);
 prefs.putUInt("04CD8CA44F6180", 226);
 prefs.putUInt("04B5C3A44F6180", 227);
 prefs.putUInt("047AE2A24F6180", 228);
 prefs.putUInt("04055DA44F6180", 229);
 prefs.putUInt("04AC8BA44F6180", 230);
 prefs.putUInt("04DE23A34F6180", 231);
 prefs.putUInt("046576A44F6181", 232);
 prefs.putUInt("043061A34F6180", 233);
 prefs.putUInt("04D9C0C16F6180", 234);
 prefs.putUInt("042988A34F6180", 235);
 prefs.putUInt("043088A34F6180", 236);
 prefs.putUInt("048194A34F6181", 237);
 prefs.putUInt("042D02A34F6180", 238);
 prefs.putUInt("047CF6A54F6180", 239);
 prefs.putUInt("04E1F5A34F6180", 240);
 prefs.putUInt("0444BFA54F6180", 241);
 prefs.putUInt("0444BFA54F6180", 242);
 prefs.putUInt("045AE0A24F6180", 243);
 prefs.putUInt("046E1CA74F6181", 244);
 prefs.putUInt("048D50A44F6180", 245);
 prefs.putUInt("04275BA44F6180", 246);
 prefs.putUInt("04FE7EA44F6180", 247);
 prefs.putUInt("042A5FA74F6180", 248);
 prefs.putUInt("041067A44F6180", 249);
 prefs.putUInt("042BF1A54F6180", 250);
 prefs.putUInt("045015A34F6180", 251);
 prefs.putUInt("04417FC36F6181", 252);
 prefs.putUInt("0437DCA34F6181", 253);
 prefs.putUInt("046155A34F6180", 254);
 prefs.putUInt("04F5C9A34F6180", 255);
 prefs.putUInt("0485B4A54F6180", 256);
 prefs.putUInt("04310BA44F6180", 257);
 prefs.putUInt("046055A34F6180", 258);
 prefs.putUInt("04C9EBA54F6180", 259);
 prefs.putUInt("0496D0A24F6180", 260);
 prefs.putUInt("04EE0BA34F6181", 261);
 prefs.putUInt("04EE0BA34F6181", 262);
 prefs.putUInt("045DB9A34F6180", 263);
 prefs.putUInt("04C110A34F6181", 264);
 prefs.putUInt("04771BA34F6181", 265);
 prefs.putUInt("0463F1A34F6180", 266);
 prefs.putUInt("046373A34F6180", 267);
 prefs.putUInt("04BE40A34F6180", 268);
 prefs.putUInt("04B91BA34F6180", 269);
 prefs.putUInt("040B03A44F6180", 270);
 prefs.putUInt("04451FA34F6180", 271);
 prefs.putUInt("04D355A34F6181", 272);
 prefs.putUInt("04D60BA64F6180", 273);
 prefs.putUInt("04D278A34F6180", 274);
 prefs.putUInt("044E94A34F6180", 275);
 prefs.putUInt("04AEDFA34F6181", 276);
 prefs.putUInt("0461B5A34F6180", 277);
 prefs.putUInt("045DBAA54F6180", 278);
 prefs.putUInt("04A587A34F6181", 279);
 prefs.putUInt("04EBC7A34F6180", 280);
 prefs.putUInt("04440BA34F6180", 281);
 prefs.putUInt("04D77DA34F6180", 282);
 prefs.putUInt("04E07CA34F6180", 283);
 prefs.putUInt("045DA5A34F6180", 284);
 prefs.putUInt("041ABFA34F6180", 285);
 prefs.putUInt("04DD99A34F6180", 286);
 prefs.putUInt("0491B0A34F6180", 287);
 prefs.putUInt("041E61A34F6180", 288);
 prefs.putUInt("04539CA54F6181", 289);
 prefs.putUInt("04FB9DA64F6180", 290);
 prefs.putUInt("0461C6A54F6180", 291);
 prefs.putUInt("04755DA64F6180", 292);
 prefs.putUInt("047400A64F6180", 293);
 prefs.putUInt("049305A34F6181", 294);
 prefs.putUInt("04D2BAA54F6180", 295);
 prefs.putUInt("04E94DC16F6180", 296);
 prefs.putUInt("04E6CCA34F6180", 297);
 prefs.putUInt("04E9A9A34F6180", 298);
 prefs.putUInt("046255A34F6180", 299);
 prefs.putUInt("04FDDFA34F6180", 300);
 prefs.putUInt("042400A44F6180", 301);
 prefs.putUInt("040EECA24F6180", 302);
 prefs.putUInt("0425C7A54F6181", 303);
 prefs.putUInt("04421FA34F6180", 304);
 prefs.putUInt("04200BA34F6180", 305);
 prefs.putUInt("042EC4A34F6181", 306);
 prefs.putUInt("0491CDA34F6180", 307);
 prefs.putUInt("049446A54F6180", 308);
 prefs.putUInt("045B78A44F6180", 309);
 prefs.putUInt("04E1CBA64F6180", 310);
 prefs.putUInt("04CC1FA34F6180", 311);
 prefs.putUInt("045589A14F6180", 312);
 prefs.putUInt("041AF6A34F6180", 313);
 prefs.putUInt("04AA04A74F6180", 314);
 prefs.putUInt("041AA7A44F6180", 315);
 prefs.putUInt("04F354A44F6180", 316);
 prefs.putUInt("0466EBA04F6180", 317);
 prefs.putUInt("044054A74F6180", 318);
 prefs.putUInt("04018CA64F6181", 319);
 prefs.putUInt("041698A44F6180", 321);
 prefs.putUInt("04EBC3A34F6180", 322);
 prefs.putUInt("0440DBA34F6180", 323);
 prefs.putUInt("04BDD3A34F6180", 324);
 prefs.putUInt("04055BA34F6180", 325);
 prefs.putUInt("04E1B5A34F6180", 326);
 prefs.putUInt("046557A34F6181", 327);
 prefs.putUInt("04B389A34F6180", 328);
 prefs.putUInt("046CE7A24F6180", 329);
 prefs.putUInt("0423BBA34F6180", 330);
 prefs.putUInt("044140A34F6181", 331);
 prefs.putUInt("04B6DFA34F6180", 332);
 prefs.putUInt("04264AA34F6180", 333);
 prefs.putUInt("043055A34F6180", 334);
 prefs.putUInt("04BDC5A34F6180", 335);
 prefs.putUInt("049DCDA34F6180", 336);
 prefs.putUInt("0420F9A24F6180", 337);
 prefs.putUInt("04DCF9A34F6181", 338);
 prefs.putUInt("04FCC9A34F6180", 339);
 prefs.putUInt("04F6CEA34F6180", 340);
 prefs.putUInt("049945A34F6180", 341);
 prefs.putUInt("0444F0A24F6180", 342);
 prefs.putUInt("04AC35A34F6180", 343);
 prefs.putUInt("04C6A5A34F6180", 344);
 prefs.putUInt("04A32AA64F6180", 345);
 prefs.putUInt("041B89A34F6180", 346);
 prefs.putUInt("047B6DA34F6180", 347);
 prefs.putUInt("041410A34F6180", 348);
 prefs.putUInt("0444ADA54F6180", 349);
 prefs.putUInt("0416F0A24F6180", 350);
 prefs.putUInt("0464F5A24F6180", 351);
 prefs.putUInt("04C4E8A34F6180", 352);
 prefs.putUInt("045934A34F6180", 353);
 prefs.putUInt("04F22FA34F6180", 354);
 prefs.putUInt("04267AA64F6180", 356);
 prefs.putUInt("0455B9A54F6180", 357);
 prefs.putUInt("04C589A34F6180", 358);
 prefs.putUInt("04DC0AA34F6181", 359);
 prefs.putUInt("04C11BA34F6181", 360);
 prefs.putUInt("049ABEA34F6180", 361);
 prefs.putUInt("0451A9A74F6180", 362);
 prefs.putUInt("046093A34F6180", 363);
 prefs.putUInt("04A772A34F6180", 364);
 prefs.putUInt("0405FAA34F6180", 365);
 prefs.putUInt("04B1B9A34F6180", 366);
 prefs.putUInt("04EBB9A34F6180", 367);
 prefs.putUInt("04DDF0A34F6180", 368);
 prefs.putUInt("0494B0A34F6180", 369);
 prefs.putUInt("04B90CA34F6180", 370);
 prefs.putUInt("04E715A34F6180", 371);
 prefs.putUInt("04C3DCA24F6180", 372);
 prefs.putUInt("041CBDA64F6181", 373);
 prefs.putUInt("04929FA54F6180", 374);
 prefs.putUInt("0465D8A34F6181", 375);
 prefs.putUInt("04D09EA34F6180", 376);
 prefs.putUInt("049160A54F6180", 377);
 prefs.putUInt("043D33A34F6180", 378);
 prefs.putUInt("042D6EA44F6180", 379);
 prefs.putUInt("0425DEA24F6181", 380);
 prefs.putUInt("04FEBCA44F6180", 381);
 prefs.putUInt("0465BBA14F6181", 382);
 prefs.putUInt("042714A54F6180", 383);
 prefs.putUInt("0403E3A34F6180", 384);
 prefs.putUInt("04F168A44F6180", 385);
 prefs.putUInt("0486DAA74F6180", 386);
 prefs.putUInt("043742A54F6181", 387);
 prefs.putUInt("040A48A44F6181", 388);
 prefs.putUInt("048132A34F6181", 389);
 prefs.putUInt("04CBD7A24F6180", 390);
 prefs.putUInt("04B995A74F6180", 391);
 prefs.putUInt("046146A14F6180", 392);
 prefs.putUInt("04D382A44F6181", 393);
 prefs.putUInt("04506DA44F6180", 394);
 prefs.putUInt("041652A44F6180", 395);

 prefs.putUInt("04DA98A34F6180", 397);
 prefs.putUInt("040DEDA34F6180", 398);
 prefs.putUInt("04DD9AA34F6180", 399);
 prefs.putUInt("04DDE0A04F6180", 400);
 prefs.putUInt("042710A34F6180", 401);
 prefs.putUInt("04719DA44F6180", 402);
 prefs.putUInt("046DA3A34F6180", 403);
 prefs.putUInt("04C751A34F6180", 404);
 prefs.putUInt("0479A2A44F6180", 405);
 prefs.putUInt("049985A74F6180", 406);
 prefs.putUInt("04835EA44F6180", 407);
 prefs.putUInt("04EB0AA74F6180", 408);
 prefs.putUInt("04E1D9A74F6180", 409);
 prefs.putUInt("04225DA54F6180", 410);
 prefs.putUInt("04F9EFA44F6180", 411);
 prefs.putUInt("045B3AA44F6180", 412);
 prefs.putUInt("0471A7A74F6180", 413);
 prefs.putUInt("04A90CA74F6180", 414);
 prefs.putUInt("04A73AA34F6180", 415);
 prefs.putUInt("049B61A34F6180", 416);
 prefs.putUInt("048732A34F6180", 417);
 prefs.putUInt("046478A44F6180", 418);
 prefs.putUInt("04DC6BA54F6181", 419);
 prefs.putUInt("0459A9A74F6180", 420);
 prefs.putUInt("048515A54F6180", 421);
 prefs.putUInt("0490F5A54F6180", 422);
 prefs.putUInt("042C01A74F6180", 423);
 prefs.putUInt("0409C4A34F6180", 424);
 prefs.putUInt("046A55A54F6180", 425);
 prefs.putUInt("046540A34F6181", 426);
 prefs.putUInt("048D8DA34F6180", 427);
 prefs.putUInt("048AE5A24F6181", 428);
 prefs.putUInt("0441B2A14F6181", 429);
 prefs.putUInt("04CA08A74F6181", 430);
 prefs.putUInt("04213BA44F6180", 431);
 prefs.putUInt("04D184A74F6180", 432);
 prefs.putUInt("04B6D8A74F6180", 433);
 prefs.putUInt("041241A54F6180", 434);
 prefs.putUInt("04C1CCA34F6181", 435);
 prefs.putUInt("04A68BA44F6180", 436);
 prefs.putUInt("044468A44F6180", 437);
 prefs.putUInt("0400B2A44F6180", 438);
 prefs.putUInt("0401C8A74F6181", 439);
 prefs.putUInt("041AEAA24F6180", 440);
 prefs.putUInt("047E08A74F6180", 441);
 prefs.putUInt("044726A34F6180", 442);
 prefs.putUInt("04C94FA74F6180", 443);
 prefs.putUInt("04E4C4A34F6180", 444);
 prefs.putUInt("04607EA54F6180", 445);
 prefs.putUInt("047CF6A44F6180", 446);
 prefs.putUInt("049346A54F6181", 447);
 prefs.putUInt("04214FA54F6180", 448);
 prefs.putUInt("046B4AA74F6180", 449);
 prefs.putUInt("042A9CA74F6180", 450);
 prefs.putUInt("04028FA44F6180", 451);
 prefs.putUInt("045215A84F6180", 452);
 prefs.putUInt("04645AA54F6180", 453);
 prefs.putUInt("04558CA74F6180", 454);
 prefs.putUInt("048005A54F6180", 455);
 prefs.putUInt("0490D3A34F6180", 456);
 prefs.putUInt("040AE7A44F6181", 457);
 prefs.putUInt("040BD1A24F6180", 458);
 prefs.putUInt("045C76A34F6181", 459);
 prefs.putUInt("04613AA44F6180", 460);
 prefs.putUInt("04AD50A44F6180", 461);
 prefs.putUInt("047E6CA34F6180", 462);
 prefs.putUInt("0451EAA34F6180", 463);
 prefs.putUInt("048DCAA74F6180", 464);
 prefs.putUInt("042121A54F6180", 465);
 prefs.putUInt("04B5F4A64F6180", 466);
 prefs.putUInt("04FE3BA54F6180", 467);
 prefs.putUInt("04EEAEA74F6181", 468);
 prefs.putUInt("04E90FA34F6180", 469);
 prefs.putUInt("045115A84F6180", 470);
 prefs.putUInt("04F5C7A44F6180", 471);
 prefs.putUInt("043239A54F6180", 472);
 prefs.putUInt("04DBD5A14F6180", 473);
 prefs.putUInt("04D3CAA44F6181", 474);
 prefs.putUInt("04D303A34F6181", 475);
 prefs.putUInt("047C62A44F6180", 476);
 prefs.putUInt("04CCD3A34F6180", 477);
 prefs.putUInt("04C471A44F6180", 478);
 prefs.putUInt("0401EDA34F6181", 479);
 prefs.putUInt("0407FFA34F6180", 480);
 prefs.putUInt("04CDFCA74F6180", 481);
 prefs.putUInt("04A221A34F6180", 482);
 prefs.putUInt("049713A74F6180", 483);
 prefs.putUInt("04B346A54F6180", 484);
 prefs.putUInt("048BE5A24F6180", 485);
 prefs.putUInt("043341A44F6180", 486);
 prefs.putUInt("043AF9A34F6180", 487);
 prefs.putUInt("042EC3A74F6181", 488);
 prefs.putUInt("045303A74F6181", 489);
 prefs.putUInt("044B04A44F6180", 490);
 prefs.putUInt("0460B5A34F6180", 491);
 prefs.putUInt("04B183A34F6180", 493);
 prefs.putUInt("04A300A84F6180", 494);
 prefs.putUInt("04F3F4A44F6180", 495);
 prefs.putUInt("045368A34F6181", 496);
 prefs.putUInt("047EEFA34F6180", 497);
 prefs.putUInt("04126FA74F6180", 498);
 prefs.putUInt("04BE2EA44F6180", 499);
 prefs.putUInt("042EEDA64F6181", 500);
 prefs.putUInt("04A029A54F6180", 501);
 prefs.putUInt("043967A44F6180", 502);
 prefs.putUInt("043967A44F6180", 502);
 prefs.putUInt("04FC4AA54F6180", 503);
 prefs.putUInt("04FC4AA54F6180", 504);
 prefs.putUInt("049718A84F6180", 505);
 prefs.putUInt("04D7B7A34F6180", 506);
 prefs.putUInt("04273AA44F6180", 507);
 prefs.putUInt("047CA7A54F6180", 508);
 prefs.putUInt("046709A84F6180", 509);
 prefs.putUInt("044596A44F6180", 510);
 prefs.putUInt("040169A44F6181", 511);
 prefs.putUInt("04D097A44F6180", 512);
 prefs.putUInt("0496CAA44F6180", 513);
 prefs.putUInt("048049A74F6180", 514);
 prefs.putUInt("04A24CA44F6180", 515);
 prefs.putUInt("04F5E9A44F6180", 516);
 prefs.putUInt("049BBBA44F6180", 517);
 prefs.putUInt("04D03DA44F6180", 518);
 prefs.putUInt("04B060A54F6180", 519);
 prefs.putUInt("04CA9AA44F6181", 520);
 prefs.putUInt("04D28FA34F6180", 521);
 prefs.putUInt("040EDEA24F6180", 522);
 prefs.putUInt("0453FDA44F6181", 523);
 prefs.putUInt("049218A34F6180", 524);
 prefs.putUInt("04F6FDA24F6180", 525);
 prefs.putUInt("0405D8A74F6180", 526);
 prefs.putUInt("04102BA34F6180", 527);
 prefs.putUInt("04D744A64F6180", 528);
 prefs.putUInt("04C497A74F6180", 529);
 prefs.putUInt("0470E8A34F6180", 530);
 prefs.putUInt("04CAEEA44F6181", 531);
 prefs.putUInt("042192A14F6180", 532);
 prefs.putUInt("046AA0A34F6180", 533);
 prefs.putUInt("04F56DA34F6180", 534);
 prefs.putUInt("041D37A34F6180", 535);
 prefs.putUInt("049710A84F6180", 536);
 prefs.putUInt("04ACFEA44F6180", 537);
 prefs.putUInt("0415DFA74F6180", 538);
 prefs.putUInt("04B0DFA74F6180", 539);
 prefs.putUInt("04CB3DA44F6180", 540);
 prefs.putUInt("04FD17A34F6180", 541);
 prefs.putUInt("04EA2AA54F6180", 542);
 prefs.putUInt("0433A1A44F6180", 543);
 prefs.putUInt("040107A34F6181", 544);
 prefs.putUInt("04A449A34F6180", 545);
 prefs.putUInt("04ABFDA34F6180", 546);
 prefs.putUInt("046ABAA34F6180", 547);
 prefs.putUInt("04DC81A14F6181", 548);
 prefs.putUInt("0463ABA44F6180", 549);
 prefs.putUInt("04D7E8A74F6180", 550);
 prefs.putUInt("04833CA44F6180", 551);
 prefs.putUInt("0431C2A44F6180", 552);
 prefs.putUInt("04B6D7A24F6180", 553);
 prefs.putUInt("041126A34F6180", 554);
 prefs.putUInt("0450A4A54F6180", 555);
 prefs.putUInt("04E726A34F6180", 556);
 prefs.putUInt("04B92EA44F6180", 558);
 prefs.putUInt("0486DBA34F6180", 559);
 prefs.putUInt("049EF8A34F6180", 560);
 prefs.putUInt("04CBE9A44F6180", 561);
 prefs.putUInt("04D17BA44F6180", 562);
 prefs.putUInt("0471D7A34F6180", 563);
 prefs.putUInt("04A55DA44F6181", 564);
 prefs.putUInt("041E04A44F6180", 565);
 prefs.putUInt("045382A44F6181", 566);
 prefs.putUInt("0406BDA74F6180", 567);
 prefs.putUInt("0420ADA64F6180", 568);
 prefs.putUInt("047E0CA34F6180", 569);
 prefs.putUInt("04D01FA34F6180", 570);
 prefs.putUInt("0412F6A74F6180", 571);
 prefs.putUInt("0412F6A74F6180", 572);
 prefs.putUInt("04BB93A34F6180", 573);
 prefs.putUInt("04D5C8A34F6180", 574);
 prefs.putUInt("04BD43A34F6180", 575);
 prefs.putUInt("0486DCA14F6180", 576);
 prefs.putUInt("04E96CA34F6180", 577);
 prefs.putUInt("041AE4A34F6180", 578);
 prefs.putUInt("045DB8A64F6180", 579);
 prefs.putUInt("040DD2A24F6180", 580);
 prefs.putUInt("04C276A34F6180", 581);
 prefs.putUInt("04ED5DA44F6180", 582);
 prefs.putUInt("047E84A34F6180", 583);
 prefs.putUInt("0479E9A74F6180", 584);
 prefs.putUInt("048292A34F6180", 585);
 prefs.putUInt("040223A34F6180", 586);
 prefs.putUInt("041761A54F6180", 587);
 prefs.putUInt("049789A44F6180", 588);
 prefs.putUInt("04E3B9A34F6180", 589);
 prefs.putUInt("04A0EEA24F6180", 590);
 prefs.putUInt("04C5EDA24F6180", 591);
 prefs.putUInt("041BBAA74F6180", 592);
 prefs.putUInt("04DE0DA34F6180", 593);
 prefs.putUInt("04E48CA44F6180", 594);
 prefs.putUInt("04495EA44F6180", 595);
 prefs.putUInt("041B18A34F6180", 596);
 prefs.putUInt("04E94AA74F6180", 597);
 prefs.putUInt("04AED8A74F6181", 598);
 prefs.putUInt("04C499A44F6180", 599);
 prefs.putUInt("04DD00A54F6180", 600);

    SPI.begin(); WiFi.mode(WIFI_STA);
    Serial.print("WiFi MAC: "); Serial.println(WiFi.macAddress());

    mfrc522.PCD_Init();
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
     pinMode(BLUE_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, LOW );
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BLUE_LED_PIN, HIGH);
    if (!rtc.begin()) {
        Serial.println("❌ Couldn't find RTC module! Check wiring.");
        while (1);
    }

    if (rtc.lostPower()) {
        Serial.println("⚠️ RTC lost power, setting to compile time.");
        rtc.adjust(DateTime(__DATE__, __TIME__));
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed");
        return;
    }

    for (int i = 0; i < numArrivalNodes; i++) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, arrivalMACs[i], 6);
        p.channel = 1;
        esp_now_add_peer(&p);
    }

    for (int i = 0; i < 4; i++) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, doctorMACs[i], 6);
        p.channel = 1;
        esp_now_add_peer(&p);
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    sharedQueue.load();
    sharedQueueA.load();
    sharedQueueB.load();
    sharedQueueC.load();

    createMixedQueue();
    printAllQueues();
}



void loop() {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
     String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
    processCard(uid);
    mfrc522.PICC_HaltA(); mfrc522.PCD_StopCrypto1(); delay(1200);
    createMixedQueue(); 
    printAllQueues();
}

String getUIDString(byte *buf, byte size) {
    String uid=""; for (byte i=0;i<size;i++)
    {if(buf[i]<0x10)uid+="0"; uid+=String(buf[i],HEX);} uid.toUpperCase(); return uid;
}

void blinkLED(int pin) {
    digitalWrite(BLUE_LED_PIN , LOW);
    digitalWrite(pin, HIGH); digitalWrite(BUZZER,HIGH ); 
    delay(500); 
    digitalWrite(BLUE_LED_PIN , HIGH);
    digitalWrite(pin, LOW); digitalWrite(BUZZER, LOW );
}


