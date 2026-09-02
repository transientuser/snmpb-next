#include "mibprofile.h"
#include "mibeffectiveplan.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace {
bool check(bool v,const char*m){if(!v)std::cerr<<"FAIL: "<<m<<'\n';return v;}
bool writeFile(const QString&p,const QByteArray&c){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(c)==c.size();}
QByteArray mib(const QByteArray&i){return i+" DEFINITIONS ::= BEGIN\nEND\n";}
}

int main(int argc,char**argv){QCoreApplication app(argc,argv);bool ok=true;QTemporaryDir temp;
 const QString repo=temp.filePath("profiles.json");MibProfileService service{MibProfileRepository(repo)};
 ok&=check(service.profiles().size()==2&&service.find(MibProfileDefinitions::allId())->type==MibProfileType::Custom,"built-ins migrate once to ordinary Profiles");
 ok&=check(service.rename(MibProfileDefinitions::allId(),"Everything I selected")&&service.remove(MibProfileDefinitions::allId()),"former All MIBs is editable and deletable");
 MibProfileService afterDelete{MibProfileRepository(repo)};ok&=check(!afterDelete.find(MibProfileDefinitions::allId()),"All MIBs is not regenerated");
 const QString emptyId=afterDelete.create("Empty exact Profile");ok&=check(afterDelete.find(emptyId)&&afterDelete.find(emptyId)->members.isEmpty()&&!afterDelete.find(emptyId)->includeStandardBase,"new Profiles begin as empty exact-file sets without implicit Standards");
 const QString root=temp.filePath("Catalog"),a=QDir(root).filePath("BatchA/rfc4188.mib"),b=QDir(root).filePath("BatchB/BRIDGE-MIB.mib"),multi=QDir(root).filePath("BatchA/vendor-file.mib");
 writeFile(a,mib("BRIDGE-MIB")+"-- A\n");writeFile(b,mib("BRIDGE-MIB")+"-- B\n");writeFile(multi,mib("VENDOR-ROOT-MIB")+mib("VENDOR-TC-MIB"));
 QString error;const QString id=afterDelete.create("Fabric Engine 9.1",&error);ok&=check(afterDelete.addFiles(id,{a,multi},MibProfileMemberReason::Added,&error),"Add Files snapshots exact files");
 const MibProfileRecord exact=*afterDelete.find(id);ok&=check(exact.members.size()==2&&exact.members.first().sha256.size()==64&&exact.explicitModules.contains("BRIDGE-MIB")&&exact.explicitModules.contains("VENDOR-TC-MIB"),"path hash and all identities persist");
 MibDependencyIndex catalog(temp.filePath("index.json"));catalog.update({root});
 const auto global=catalog.providersFor("BRIDGE-MIB"),scoped=catalog.providersFor("BRIDGE-MIB",QFileInfo(a).absolutePath(),MibCatalogProviderScope::ExactFolder);
 ok&=check(global.size()==2&&scoped.size()==1&&scoped.first().canonicalPath==QFileInfo(a).canonicalFilePath(),"Catalog duplicates remain distinct and folder lookup stays scoped");
 const auto planA=MibEffectivePlanResolver().resolve(exact,catalog);ok&=check(planA.member("BRIDGE-MIB")&&planA.member("BRIDGE-MIB")->provider.canonicalPath==QFileInfo(a).canonicalFilePath(),"Profile selects exact provider A");
 MibProfileRecord exactB=exact;exactB.members=MibProfileMembersFromFiles({b});exactB.explicitModules=MibProfileMemberIdentities(exactB.members);const auto planB=MibEffectivePlanResolver().resolve(exactB,catalog);
 ok&=check(planB.member("BRIDGE-MIB")&&planB.member("BRIDGE-MIB")->provider.canonicalPath==QFileInfo(b).canonicalFilePath()&&planA.sha256!=planB.sha256,"provider B cannot silently replace A");
 MibProfileService reload{MibProfileRepository(repo)};const MibProfileRecord saved=*reload.find(id);ok&=check(saved.members==exact.members,"schema-4 exact membership round trips");
 const QString substitute=QDir(root).filePath("BatchA/provider-with-old-content.mib");writeFile(substitute,mib("BRIDGE-MIB")+"-- A\n");writeFile(a,mib("BRIDGE-MIB")+"-- changed\n");ok&=check(MibProfileMemberCurrentState(saved.members.first())==MibProfileMemberState::Changed,"content change detected");catalog.update({root});const auto changedPlan=MibEffectivePlanResolver().resolve(saved,catalog);ok&=check(changedPlan.missingModules.contains("BRIDGE-MIB")&&!changedPlan.member("BRIDGE-MIB")->provider.canonicalPath.size(),"reindex preserves expected hash and cannot substitute same identity with old content elsewhere");QFile::remove(multi);
 auto mm=std::find_if(saved.members.cbegin(),saved.members.cend(),[](const auto&m){return m.identities.contains("VENDOR-ROOT-MIB");});ok&=check(mm!=saved.members.cend()&&MibProfileMemberCurrentState(*mm)==MibProfileMemberState::Missing,"missing file preserves intent");
 const QString folder=QDir(root).filePath("Folder"),one=QDir(folder).filePath("one.mib"),two=QDir(folder).filePath("nested/two.mib");writeFile(one,mib("ONE-MIB"));writeFile(two,mib("TWO-MIB"));
 const QString iid=reload.create("Individual",&error),fid=reload.create("Folder",&error);reload.addFiles(iid,{one,two},MibProfileMemberReason::Added,&error);reload.addFolder(fid,folder,&error);
 ok&=check(reload.find(iid)->members==reload.find(fid)->members,"recursive Add Folder equals individual files");const auto snapshot=reload.find(fid)->members;writeFile(QDir(folder).filePath("later.mib"),mib("LATER-MIB"));ok&=check(reload.find(fid)->members==snapshot,"later addition does not regenerate");QFile::remove(one);const auto removed=std::find_if(reload.find(fid)->members.cbegin(),reload.find(fid)->members.cend(),[&one](const auto&m){return m.canonicalPath==QFileInfo(one).absoluteFilePath();});ok&=check(reload.find(fid)->members==snapshot&&removed!=reload.find(fid)->members.cend()&&MibProfileMemberCurrentState(*removed)==MibProfileMemberState::Missing,"later removal remains missing intent");
 const QString legacyPath=temp.filePath("legacy.json");writeFile(legacyPath,QJsonDocument(QJsonObject{{"schemaVersion",3},{"profiles",QJsonArray{QJsonObject{{"id","legacy-folder"},{"name","Legacy Product"},{"type","folder"},{"directory",folder}}}}}).toJson());
 MibProfileService legacy{MibProfileRepository(legacyPath)};ok&=check(legacy.refreshAutomaticProfiles(root,&error)&&legacy.find("legacy-folder")&&legacy.find("legacy-folder")->type==MibProfileType::Custom,"Automatic migrates to editable snapshot with stable ID");const auto migrated=legacy.find("legacy-folder")->members;writeFile(QDir(folder).filePath("post.mib"),mib("POST-MIB"));legacy.refreshAutomaticProfiles(root,&error);ok&=check(legacy.find("legacy-folder")->members==migrated,"migrated Profile never regenerates");
 const QString legacyRoot=temp.filePath("LegacyCatalog"),unique=QDir(legacyRoot).filePath("unique.mib"),dupA=QDir(legacyRoot).filePath("same/provider-A.mib"),dupB=QDir(legacyRoot).filePath("same/provider-B.mib");writeFile(unique,mib("UNIQUE-MIB"));writeFile(dupA,mib("DUP-MIB")+"-- A\n");writeFile(dupB,mib("DUP-MIB")+"-- B\n");MibDependencyIndex legacyIndex(temp.filePath("legacy-index.json"));legacyIndex.update({legacyRoot});
 const QString identityRepo=temp.filePath("identity-profiles.json");writeFile(identityRepo,QJsonDocument(QJsonObject{{"schemaVersion",3},{"profiles",QJsonArray{QJsonObject{{"id","identity-only"},{"name","Identity Only"},{"type","custom"},{"modules",QJsonArray{"UNIQUE-MIB","DUP-MIB","ABSENT-MIB"}}}}}}).toJson());MibProfileService identityProfiles{MibProfileRepository(identityRepo)};ok&=check(MibProfileRequiresExactMigration(*identityProfiles.find("identity-only")),"identity-only persistence is classified as migration data, not current authority");ok&=check(identityProfiles.migrateLegacyProfiles(legacyIndex,&error),"legacy identity Profile conversion persists atomically");const auto *converted=identityProfiles.find("identity-only");ok&=check(converted&&!MibProfileRequiresExactMigration(*converted)&&converted->members.size()==1&&converted->members.first().identities.contains("UNIQUE-MIB")&&converted->unresolvedLegacyModules==QStringList({"ABSENT-MIB","DUP-MIB"}),"legacy migration selects only unique exact providers and preserves unresolved intent");
 QFile::remove(dupB);legacyIndex.update({legacyRoot});ok&=check(identityProfiles.migrateLegacyProfiles(legacyIndex,&error)&&identityProfiles.find("identity-only")->members.size()==2&&identityProfiles.find("identity-only")->unresolvedLegacyModules==QStringList{"ABSENT-MIB"},"unresolved legacy membership retries when Catalog authority becomes unique");MibProfileService identityReload{MibProfileRepository(identityRepo)};ok&=check(identityReload.find("identity-only")->members.size()==2&&identityReload.find("identity-only")->unresolvedLegacyModules==QStringList{"ABSENT-MIB"},"exact conversion and unresolved legacy intent survive restart");const QString forbiddenId=identityReload.create("No legacy authority",&error);MibProfileRecord forbidden=*identityReload.find(forbiddenId);forbidden.explicitModules={"IDENTITY-ONLY-MIB"};ok&=check(!identityReload.update(forbidden,&error),"normal Profile editing cannot create identity-only runtime authority");
 QFile json(repo);ok&=check(json.open(QIODevice::ReadOnly)&&json.readAll().contains("\"schemaVersion\": 4")&&MibProfileRepository(repo).ordinaryProfileMigrationComplete(),"schema-4 marker prevents regeneration");return ok?0:1;}
