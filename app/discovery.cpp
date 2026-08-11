/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <qmessagebox.h>
#include <QContextMenuEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <functional>

#include "discovery.h"
#include "communitycredentialservice.h"
#include "preferences.h"
#include "agent.h"
#include "agentprofileservice.h"
#include "discoverydestination.h"
#include "snmp_pp/snmp_pp.h"
#include "snmp_pp/snmpmsg.h"

#define DISC_SNMP_V1  "V1"
#define DISC_SNMP_V2C "V2c"
#define DISC_SNMP_V3  "V3"

namespace {
class FunctionalDiscoveryExecutor final : public IDiscoveryProbeExecutor
{
public:
    using Function = std::function<DiscoveryProbeResult(
        const DiscoveryProbePlan &, int)>;
    explicit FunctionalDiscoveryExecutor(Function function)
        : function(std::move(function)) {}
    DiscoveryProbeResult execute(const DiscoveryProbePlan &probe,
                                 int waitTime) override
    { return function(probe, waitTime); }
private:
    Function function;
};
}

/* Conversion routines to/from bytes array to integer value for IP addresses */
#define IPV4_FROM_UCHAR(array, integer)                         \
    do {                                                        \
        for (int _a = 4; _a > 0; _a--)                          \
            integer |= (unsigned int)array[_a-1] << ((4-_a)*8); \
    } while(0)

#define IPV6_FROM_UCHAR(array, hillint, lollint)                      \
    do {                                                              \
        for (int _a = 8; _a > 0; _a--)                                \
        {                                                             \
            hillint |= (unsigned long long)array[_a-1] << ((8-_a)*8); \
            lollint |= (unsigned long long)array[_a+7] << ((8-_a)*8); \
        }                                                             \
    } while(0)

#define IPV4_TO_UCHAR(array, integer)                     \
    do {                                                  \
        for (int _a = 4; _a > 0; _a--)                    \
            array[_a-1] = (integer >> ((4-_a)*8)) & 0xFF; \
    } while(0)

#define IPV6_TO_UCHAR(array, hillint, lollint)            \
    do {                                                  \
        for (int _a = 8; _a > 0; _a--)                    \
        {                                                 \
            array[_a-1] = (hillint >> ((8-_a)*8)) & 0xFF; \
            array[_a+7] = (lollint >> ((8-_a)*8)) & 0xFF; \
        }                                                 \
    } while(0)

/* Internal SNMP++ routine */
extern int receive_snmp_response(SnmpSocket sock, Snmp &snmp_session,
                                 Pdu &pdu, UdpAddress &fromaddress,
                                 OctetStr &engine_id, bool process_msg = true);

static unsigned char snmpv3_broadcast_message[] =
{
    0x30, 0x3a,
    0x02, 0x01, 0x03,             // Version: 3
    0x30, 0x0f,                   // global header length 15
    0x02, 0x03, 0x01, 0x00, 0x00, // message id
    0x02, 0x02, 0x10, 0x00,       // message max size
    0x04, 0x01, 0x04,             // flags (reportable set)
    0x02, 0x01, 0x03,             // security model USM
    0x04, 0x10,                   // security params
    0x30, 0x0e,
    0x04, 0x00,                   // no engine id
    0x02, 0x01, 0x00,             // boots 0
    0x02, 0x01, 0x00,             // time 0
    0x04, 0x00,                   // no user name
    0x04, 0x00,                   // no auth par
    0x04, 0x00,                   // no priv par
    0x30, 0x12,
    0x04, 0x00,                   // no context engine id
    0x04, 0x00,                   // no context name
    0xa0, 0x0c,                   // GET PDU
    0x02, 0x02, 0x34, 0x26,       // request id
    0x02, 0x01, 0x00,             // error status no error
    0x02, 0x01, 0x00,             // error index 0
    0x30, 0x00                    // no data
};

static const char *info_oids[] =
{
    "1.3.6.1.2.1.1.1.0", // Description
    "1.3.6.1.2.1.1.3.0", // Uptime
    "1.3.6.1.2.1.1.4.0", // Contact
    "1.3.6.1.2.1.1.5.0", // Name
    "1.3.6.1.2.1.1.6.0"  // Location
};

Discovery::Discovery(Snmpb *snmpb)
{
    s = snmpb;

    destinationFolder = new QComboBox(s->MainUI()->DiscoveryAgentProperties);
    auto *destinationLabel = new QLabel(tr("Discovery &destination:"),
                                        s->MainUI()->DiscoveryAgentProperties);
    destinationLabel->setBuddy(destinationFolder);
    if (auto *layout = qobject_cast<QGridLayout *>(
            s->MainUI()->DiscoveryAgentProperties->layout()))
    {
        layout->addWidget(destinationLabel, 1, 0);
        layout->addWidget(destinationFolder, 1, 1, 1, 4);
    }
    RefreshDestinationFolders();
    connect(destinationFolder, &QComboBox::currentIndexChanged, this, [this]() {
        QSettings settings;
        DiscoveryDestinationSettings::save(
            settings, destinationFolder->currentData().toString());
    });

    connect( s->MainUI()->DiscoveryButton,
             SIGNAL( clicked() ), this, SLOT( Discover() ));
    connect( s->MainUI()->DiscoveryAbortButton, 
             SIGNAL( clicked() ), this, SLOT( Abort() ));
    connect( s->MainUI()->DiscoveryAgentSettings, 
             SIGNAL( clicked() ), this, SLOT( ShowAgentSettings() ));
    connect( s->APManagerObj(), SIGNAL( AgentProfileListChanged() ), 
             this, SLOT ( AgentProfileListChange() ) );

    // Fill-in the list of agent profiles from profiles manager
    AgentProfileListChange();

    // Create the discovery thread (not started)
    dt = new DiscoveryThread(s);

    connect( dt, SIGNAL( SendAgent(QStringList) ), 
             this, SLOT( DisplayAgent(QStringList) ));
    connect( dt, SIGNAL( SignalStartStop(int) ), 
             this, SLOT( StartStop(int) ));
    connect( dt, SIGNAL( SignalProgress(int) ), 
             this, SLOT( DisplayProgress(int) ));

    // Create context menu actions
    s->MainUI()->DiscoveryOutput->setContextMenuPolicy (Qt::CustomContextMenu);
    connect( s->MainUI()->DiscoveryOutput, 
             SIGNAL( customContextMenuRequested ( const QPoint & ) ),
             this, SLOT( ContextMenu ( const QPoint & ) ) );
    addAgentAct = new QAction(tr("&Add agent(s) to profile list"), this);
    connect(addAgentAct, SIGNAL(triggered()), this, SLOT(AddAgentToProfiles()));
    s->MainUI()->DiscoveryButton->setText(tr("Start Scan"));
    s->MainUI()->DiscoveryAbortButton->setText(tr("Stop"));
    s->MainUI()->DiscoveryButton->setToolTip(tr("Start discovery with the selected profile and destination"));
    s->MainUI()->DiscoveryAbortButton->setToolTip(tr("Stop the active discovery scan"));
}

Discovery::~Discovery()
{
    dt->Abort();
    dt->wait();
}

void Discovery::RefreshDestinationFolders(void)
{
    QSettings settings;
    const QString saved = DiscoveryDestinationSettings::load(settings);
    const QList<DeviceFolderChoice> folders = s->DevicePlacements()->folderChoices();
    const QString resolved = DiscoveryDestinationSettings::resolve(saved, folders);
    const QSignalBlocker blocker(destinationFolder);
    destinationFolder->clear();
    destinationFolder->addItem(tr("Unfiled"), QString());
    for (const DeviceFolderChoice &folder : folders)
        destinationFolder->addItem(folder.displayPath, folder.folderId);
    const int index = destinationFolder->findData(resolved);
    destinationFolder->setCurrentIndex(index >= 0 ? index : 0);
    if (resolved != saved)
        DiscoveryDestinationSettings::save(settings, QString());
}

void Discovery::ShowAgentSettings(void)
{
     s->APManagerObj()->SetSelectedAgentById(
         s->MainUI()->DiscoveryAgentProfile->currentData().toString());
     s->APManagerObj()->Execute();
}

void Discovery::AgentProfileListChange(void)
{
    QString cap = s->MainUI()->DiscoveryAgentProfile->currentData().toString();
    s->MainUI()->DiscoveryAgentProfile->clear();
    const QList<AgentProfileRecord> profiles =
        s->APManagerObj()->GetAgentProfileRecords();
    for (const AgentProfileRecord &profile : profiles)
        s->MainUI()->DiscoveryAgentProfile->addItem(profile.name,
                                                    profile.profileId);
    if (cap.isEmpty() == false)
    {
        int idx = s->MainUI()->DiscoveryAgentProfile->findData(cap);
        s->MainUI()->DiscoveryAgentProfile->setCurrentIndex(idx>=0?idx:0);
    }
}

DiscoveryThread::DiscoveryThread(QObject *parent):QThread(parent)
{
    snmp = nullptr;
    status = SNMP_CLASS_ERROR;
};

DiscoveryThread::~DiscoveryThread()
{
    Abort();
    wait();
}

void DiscoveryThread::Configure(const DiscoveryScanPlan &plan)
{
    scanPlan = plan;
    cancellation = std::make_shared<SnmpCancellationToken>();
}

void DiscoveryThread::SendAgentInfo(Pdu pdu, UdpAddress a, snmp_version v)
{
    QStringList agent_info;
    QString name;
    QString address;
    QString protocol;
    QString uptime;
    QString contact;
    QString location;
    QString description;

    Vb vb;

    for (int k = 0; k < 5; k++)
    {
        pdu.get_vb(vb, k);
        if (k == 1)
        {
            unsigned long time = 0;
            vb.get_value(time);
            TimeTicks ut(time);
            uptime = ut.get_printable(); 
        }
        else
        {
            static unsigned char buf[5000];
            unsigned long len;
            vb.get_value(buf, len, 5000);
            buf[len] = '\0';
            switch(k)
            {
                case 0: description = (const char*)buf; break;
                case 2: contact = (const char*)buf; break;
                case 3: name = (const char*)buf; break;
                case 4: location = (const char*)buf; break;
                default: break;
            }
        }
    }

    address = a.get_printable();
    protocol = (v == version3)?DISC_SNMP_V3: 
               ((v == version2c)?DISC_SNMP_V2C:DISC_SNMP_V1);

    agent_info.clear();
    agent_info << name << address << protocol << uptime 
               << contact << location << description;

    emit SendAgent(agent_info);
}

void DiscoveryThread::Progress(void)
{
    emit SignalProgress(++current_progress);
}

void DiscoveryThread::Abort(void)
{
    if (cancellation) cancellation->cancel();
}

DiscoverySnmp::DiscoverySnmp(int &status, const UdpAddress &addr)
    :Snmp(status, addr)
{
}

DiscoverySnmp::DiscoverySnmp(int &status, const UdpAddress& addr_v4, 
    const UdpAddress& addr_v6):Snmp(status, addr_v4, addr_v6)
{
}

void DiscoverySnmp::discover(const UdpAddress &start_addr, unsigned long long num_addr,
                             const int timeout_sec, const snmp_version version,
                             const SnmpRequestConfig &config,
                             bool use_snmpv3_probe, DiscoveryThread* thread,
                             const SnmpCancellationToken &cancellation)
{
    unsigned char *message = NULL;
    int message_length = 0;
    unsigned int sock;
    SnmpMessage *snmpmsg = NULL;
    Pdu pdu;
    OctetStr get_community;
    unsigned long long hicuraddr = 0, locuraddr = 0;
    unsigned int curaddr = 0;

    // Prepare pdu
    if ((version != version3) || (use_snmpv3_probe == false))
    {
        Vb vb;

        for (int k = 0; k < 5; k++)
        { 
            vb.set_oid(Oid(info_oids[k]));
            pdu += vb;
        }

        pdu.set_error_index(0);            // set error index to none
        pdu.set_type(sNMP_PDU_GET);        // set pdu type

        if (version != version3)
        {
            get_community = config.readCommunity.toLatin1().data();
        }
        else
        {
            // set the security level to use
            if (config.securityLevel == 0/*"noAuthNoPriv"*/)
                pdu.set_security_level(SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV);
            else if (config.securityLevel == 1/*"authNoPriv"*/)
                pdu.set_security_level(SNMP_SECURITY_LEVEL_AUTH_NOPRIV);
            else
                pdu.set_security_level(SNMP_SECURITY_LEVEL_AUTH_PRIV);

            pdu.set_context_name(config.contextName.toLatin1().data());
            pdu.set_context_engine_id(config.contextEngineId.toLatin1().data());
        }
    }

    // Send probe packets
    UdpAddress cur_address = start_addr;

    // First, convert the address to incrementable integer(s)
    if (cur_address.get_ip_version() == Address::version_ipv4)
    {
        sock = iv_snmp_session;
        IPV4_FROM_UCHAR(cur_address, curaddr);
    }
    else
    {
        sock = iv_snmp_session_ipv6;
        IPV6_FROM_UCHAR(cur_address, hicuraddr, locuraddr);
    }

    for(unsigned long long j = 0; j < num_addr; j++)
    {
        if (version != version3)
        {
            pdu.set_request_id(MyMakeReqId()); // determine request id to use

            snmpmsg = new SnmpMessage();
            if (snmpmsg->load(pdu, get_community, version) != SNMP_CLASS_SUCCESS)
                goto next_addr;

            message        = snmpmsg->data();
            message_length = snmpmsg->len();
        }
        else
        {
            if (use_snmpv3_probe == true)
            {
                unsigned short v3reqid = MyMakeReqId();
                message = (unsigned char *)snmpv3_broadcast_message;
                message_length = sizeof(snmpv3_broadcast_message);
                message[50] = v3reqid >> 8;
                message[51] = v3reqid & 0xFF;
            }
            else
            {
                OctetStr engine_id;
                pdu.set_request_id(MyMakeReqId()); // determine request id to use
                v3MP::I->get_from_engine_id_table(engine_id, 
                                                  cur_address.get_printable());

                snmpmsg = new SnmpMessage();
                if (snmpmsg->loadv3( pdu, engine_id, config.securityName.toLatin1().data(),
                                     SNMP_SECURITY_MODEL_USM, 
                                     version) != SNMP_CLASS_SUCCESS)
                    goto next_addr;

                message        = snmpmsg->data();
                message_length = snmpmsg->len();
            }
        }

        if (send_raw_data(message, message_length, cur_address) < 0)
        {
            if (snmpmsg) delete snmpmsg;
            return;
        }

next_addr:
        if (snmpmsg)
        {
            delete snmpmsg;
            snmpmsg = NULL;
        }

        // Increment the address according to transport protocol version
        if (cur_address.get_ip_version() == Address::version_ipv4)
        {
            curaddr++;
            IPV4_TO_UCHAR(cur_address, curaddr);
        }
        else
        {
            if (locuraddr == 0xFFFFFFFFFFFFFFFFULL) // locuraddr overflow
                hicuraddr++;
            locuraddr++;
            IPV6_TO_UCHAR(cur_address, hicuraddr, locuraddr);
        }

        if (cancellation.isCancelled())
            return;
    }

    // Now wait for the responses
    Pdu in_pdu;
    fd_set readfds;
    int nfound = 0;
    struct timeval fd_timeout;
    msec end_time;

    end_time += 1000;
    int num_sec = 1;

    lock();
    do
    {
new_loop:
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        end_time.GetDeltaFromNow(fd_timeout);

        nfound = select((int)(sock + 1), &readfds, NULL, NULL, &fd_timeout);

        if ((nfound > 0) && (FD_ISSET(sock, &readfds)))
        {
            // Received a message
            UdpAddress from;
            OctetStr engine_id;
            int res = receive_snmp_response(sock, *this, in_pdu, from, 
                                            engine_id, true);

            if((res == SNMPv3_MP_UNKNOWN_PDU_HANDLERS) || // SNMPv3
               (res == SNMP_CLASS_SUCCESS)) // SNMPv1 or SNMPv2c
                thread->SendAgentInfo(in_pdu, from, version);
        }

        if (cancellation.isCancelled())
        {
            unlock();
            return;
        }

        // A second as elapsed, show progress
        if ((fd_timeout.tv_sec == 0) && (fd_timeout.tv_usec == 0))
        {
            thread->Progress();
            if (num_sec < timeout_sec)
            {
                end_time += 1000;
                num_sec++;
                goto new_loop;
            }
        }

    } while ((fd_timeout.tv_sec > 0) || (fd_timeout.tv_usec > 0));
    unlock();
}

void DiscoveryThread::run(void)
{
    if (scanPlan.enableIpv4 && scanPlan.enableIpv6)
        snmp = new DiscoverySnmp(status, UdpAddress("0.0.0.0"), UdpAddress("::"));
    else if (scanPlan.enableIpv4)
        snmp = new DiscoverySnmp(status, UdpAddress("0.0.0.0"));
    else if (scanPlan.enableIpv6)
        snmp = new DiscoverySnmp(status, UdpAddress("::"));
    else
        status = SNMP_CLASS_ERROR;
    if (!snmp || status != SNMP_CLASS_SUCCESS)
    {
        delete snmp; snmp = nullptr;
        emit SignalStartStop(0);
        return;
    }
    emit SignalStartStop(1);
    current_progress = 0;
    FunctionalDiscoveryExecutor executor([this](const DiscoveryProbePlan &probe,
                                                 int waitTime) {
        snmp_version version = version1;
        if (probe.requestConfig.version == SnmpRequestVersion::V2c) version = version2c;
        else if (probe.requestConfig.version == SnmpRequestVersion::V3) version = version3;
        snmp->discover(UdpAddress(probe.startEndpoint.toLatin1().constData()),
                       probe.addressCount, waitTime, version,
                       probe.requestConfig, probe.useSnmpV3Probe, this,
                       *cancellation);
        return DiscoveryProbeResult{
            cancellation->isCancelled() ? DiscoveryCompletion::Cancelled
                                        : DiscoveryCompletion::Complete,
            cancellation->isCancelled() ? SnmpOperationStatus::Cancelled
                                        : SnmpOperationStatus::Complete,
            {}, waitTime};
    });
    DiscoveryOperation(scanPlan).execute(executor, *cancellation);
    delete snmp;
    snmp = nullptr;
    emit SignalStartStop(0);
}

void Discovery::DisplayAgent(QStringList agent_info)
{
    // Check if the agent already exists in the list
    QList<QTreeWidgetItem *> laddr = 
        s->MainUI()->DiscoveryOutput->findItems(agent_info[1], 
                                                Qt::MatchExactly, 1);

    // If it exists, add the new supported protocol to its list.
    if (!laddr.isEmpty())
    {
        if (!((agent_info[2] == DISC_SNMP_V1) && 
              strstr(laddr[0]->text(2).toLatin1().data(), DISC_SNMP_V1)) && 
            !((agent_info[2] == DISC_SNMP_V2C) && 
              strstr(laddr[0]->text(2).toLatin1().data(), DISC_SNMP_V2C)) && 
            !((agent_info[2] == DISC_SNMP_V3) && 
              strstr(laddr[0]->text(2).toLatin1().data(), DISC_SNMP_V3)))
        laddr[0]->setText(2,  laddr[0]->text(2) + "/" + agent_info[2]);
    }
    else
    {
        // Else add the new agent to the list, as is.
        QTreeWidgetItem *val = new QTreeWidgetItem(s->MainUI()->DiscoveryOutput,
                                                   agent_info);
        val->setData(0, Qt::UserRole, activePlan.templateProfileId);
        val->setData(0, Qt::UserRole + 1, activePlan.destinationFolderId);
        s->MainUI()->DiscoveryOutput->addTopLevelItem(val);
    }
}

void Discovery::StartStop(int isstart)
{
    const bool editable = !isstart;
    s->MainUI()->DiscoveryAgentProperties->setEnabled(editable);
    s->MainUI()->DiscoveryLocal->setEnabled(editable);
    s->MainUI()->DiscoveryNetworks->setEnabled(editable);
    s->MainUI()->DiscoveryFrom->setEnabled(editable);
    s->MainUI()->DiscoveryTo->setEnabled(editable);
    if (isstart)
    {
        s->MainUI()->DiscoveryButton->setEnabled(false);
        s->MainUI()->DiscoveryAbortButton->setEnabled(true);
    }
    else
    {
        s->MainUI()->DiscoveryButton->setEnabled(true);
        s->MainUI()->DiscoveryAbortButton->setEnabled(false);
    }
}

void Discovery::DisplayProgress(int value)
{
    s->MainUI()->DiscoveryProgress->setValue(value);
}

void Discovery::Abort(void)
{
    dt->Abort();
}

void Discovery::ContextMenu ( const QPoint &pos )
{    
    QMenu menu(tr("Actions"), s->MainUI()->DiscoveryOutput);

    menu.addAction(addAgentAct);

    menu.exec(s->MainUI()->DiscoveryOutput->mapToGlobal(pos));
}

void Discovery::AddAgentToProfiles(void)
{
    QList<QTreeWidgetItem *> item_list = 
                             s->MainUI()->DiscoveryOutput->selectedItems();
    char buf[52]; // for IPv6 addr/port = 45+/+5+NULL

    for (int i = 0; i < item_list.size(); i++)
    {
        const QString templateId = item_list[i]->data(0, Qt::UserRole).toString();
        const QString destinationId = item_list[i]->data(
            0, Qt::UserRole + 1).toString();
        strcpy(buf, item_list[i]->text(1).toLatin1().data());
        QString address(strtok(buf, "/"));

        const QString profileId = s->AgentProfiles()->createFromTemplate(
            templateId,
            item_list[i]->text(0).isEmpty() ? address : item_list[i]->text(0),
            address,
            QString(strstr(item_list[i]->text(1).toLatin1().data(), "/") + 1),
            strstr(item_list[i]->text(2).toLatin1().data(), DISC_SNMP_V1) != nullptr,
            strstr(item_list[i]->text(2).toLatin1().data(), DISC_SNMP_V2C) != nullptr,
            strstr(item_list[i]->text(2).toLatin1().data(), DISC_SNMP_V3) != nullptr);
        if (!profileId.isEmpty())
        {
            s->CommunityCredentials()->copyBinding(templateId, profileId);
            s->DevicePlacements()->placeProfile(profileId, destinationId);
        }
    }
}


void Discovery::Discover(void)
{
    if (dt->isRunning()) return;
    int num_transport = 0;
    int num_proto = 0;
    unsigned long long num_addresses = 0;

    if (s->MainUI()->DiscoveryV1->isChecked()) num_proto++;
    if (s->MainUI()->DiscoveryV2c->isChecked()) num_proto++;
    if (s->MainUI()->DiscoveryV3->isChecked()) num_proto++;

    if (num_proto < 1)
        return;

    const int wait_time = s->MainUI()->DiscoveryWaitTime->value();

    s->MainUI()->DiscoveryProgress->reset();
    s->MainUI()->DiscoveryOutput->clear();

    if (s->MainUI()->DiscoveryLocal->isChecked())
    {
        if (s->PreferencesObj()->GetEnableIPv4()) num_transport++;
        if (s->PreferencesObj()->GetEnableIPv6()) num_transport++;
        num_addresses = 1;
    }
    else
    {
        IpAddress addr_from(s->MainUI()->DiscoveryFrom->text().toLatin1().data());
        IpAddress addr_to(s->MainUI()->DiscoveryTo->text().toLatin1().data());

        // From must be a valid IP address
        if (!addr_from.valid() || (addr_from[0] == 0))
        {
            QMessageBox::critical(nullptr,
                                  tr("\"From\" address"),
                                  tr("Invalid Address: %1")
                                    .arg(s->MainUI()->DiscoveryFrom->text()),
                                  QMessageBox::Ok);
            return;
        }

        // To must be a valid IP address
        if (!addr_to.valid() || (addr_to[0] == 0))
        {
            QMessageBox::critical(nullptr,
                                  tr("\"To\" address"),
                                  tr("Invalid Address: %1")
                                    .arg(s->MainUI()->DiscoveryTo->text()),
                                  QMessageBox::Ok);
            return;
        }

        Address::version_type vfrom = addr_from.get_ip_version();
        Address::version_type vto = addr_to.get_ip_version();

        // IP versions must match
        if (vfrom != vto) 
        {
            QMessageBox::critical(nullptr,
                                  tr("IP version mismatch"),
                                  tr("IP address version mismatch: %1 -> %2")
                                    .arg(vfrom==Address::version_ipv4?"IPv4":"IPv6")
                                    .arg(vto==Address::version_ipv4?"IPv4":"IPv6"),
                                  QMessageBox::Ok);
            return;
        }

        // Do checks based on IP version and compute the number of addresses
        if (vfrom == Address::version_ipv4)
        {
            unsigned int from = 0, to = 0;

            if (s->PreferencesObj()->GetEnableIPv4() == false)
            {
                QMessageBox::critical(nullptr,
                                      tr("IP transport"),
                                      tr("IPv4 address specified but transport is "
                                         "unavailable (see Options menu->Preferences->Transport)"),
                                      QMessageBox::Ok);
                return;
            }

            IPV4_FROM_UCHAR(addr_from, from);
            IPV4_FROM_UCHAR(addr_to, to);

            if (from <= to)
                num_addresses = to - from + 1;
            else
            {
                QMessageBox::critical(nullptr,
                                      tr("Invalid address range"),
                                      tr("'To address' must be greater than 'From address'"),
                                      QMessageBox::Ok);
                return;
            }
        }
        else
        if (vfrom == Address::version_ipv6)
        {
            unsigned long long lofrom = 0, hifrom = 0, loto = 0, hito = 0;

            if (s->PreferencesObj()->GetEnableIPv6() == false)
            {
                QMessageBox::critical(nullptr,
                                      tr("IP transport"),
                                      tr("IPv6 address specified but transport is "
                                         "unavailable (see Options menu->Preferences->Transport)"),
                                      QMessageBox::Ok);
                return;
            }

            IPV6_FROM_UCHAR(addr_from, hifrom, lofrom);
            IPV6_FROM_UCHAR(addr_to, hito, loto);

            if (((hito - hifrom) == 0) && (lofrom <= loto))
                num_addresses = loto - lofrom + 1;
            else if (((hito - hifrom) == 1) && (lofrom > loto))
                num_addresses = ~lofrom + loto + 1;
            else
            {
                QMessageBox::critical(nullptr,
                                      tr("Invalid address range"),
                                      tr("'To address' must be greater than 'From"
                                         " address' with a range no larger than 2^64 addresses"),
                                      QMessageBox::Ok);
                return;
            }
        }
        else
        {
            QMessageBox::critical(nullptr,
                                  tr("Transport"),
                                  tr("Unsupported transport type"),
                                  QMessageBox::Ok);
            return;
        }

        num_transport = 1;
    }

    activePlan = {};
    activePlan.templateProfileId =
        s->MainUI()->DiscoveryAgentProfile->currentData().toString();
    const AgentProfileRecord *profile = s->AgentProfiles()->findById(
        activePlan.templateProfileId);
    if (!profile) return;
    activePlan.templateProfile = *profile;
    activePlan.destinationFolderId = destinationFolder->currentData().toString();
    activePlan.waitTimeSeconds = wait_time;
    activePlan.enableIpv4 = s->PreferencesObj()->GetEnableIPv4();
    activePlan.enableIpv6 = s->PreferencesObj()->GetEnableIPv6();
    QStringList endpoints;
    if (s->MainUI()->DiscoveryLocal->isChecked())
    {
        if (activePlan.enableIpv4) endpoints.append("255.255.255.255/" + profile->port);
        if (activePlan.enableIpv6) endpoints.append("ff02::1/" + profile->port);
    }
    else
        endpoints.append(s->MainUI()->DiscoveryFrom->text() + "/" + profile->port);
    const EffectiveCredentialValues effective =
        s->CommunityCredentials()->resolve(*profile).values;
    const bool protocols[] = {s->MainUI()->DiscoveryV1->isChecked(),
                              s->MainUI()->DiscoveryV2c->isChecked(),
                              s->MainUI()->DiscoveryV3->isChecked()};
    for (const QString &endpoint : endpoints)
        for (int protocol = 0; protocol < 3; ++protocol)
            if (protocols[protocol])
            {
                SnmpRequestConfig config;
                if (protocol < 2)
                    SnmpRequestConfig::FromProfile(*profile, protocol, effective, &config);
                else
                    SnmpRequestConfig::FromProfile(*profile, protocol, &config);
                activePlan.probes.append({endpoint, num_addresses, config,
                    protocol == 2 && s->MainUI()->DiscoverySNMPv3Probe->isChecked()});
            }
    s->MainUI()->DiscoveryProgress->setRange(0,
        num_transport * num_proto * wait_time);
    dt->Configure(activePlan);
    dt->start();
}

