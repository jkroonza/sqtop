/*
 * Idea taken from squid_stat by StarSoft Ltd.
 * (C) 2006 Oleg V. Palij <o.palij@gmail.com>
 * Released under the GNU GPL, see the COPYING file in the source distribution for its full text.
 */

//atoll,atoi,sort
#include <algorithm>
//stringstream
#include <sstream>
// for "compactsameurls" option
#include <map>
#include <arpa/inet.h>
#include <iostream>
#include <unistd.h>

#include "sqconn.hpp"
#include "sqstat.hpp"
#include "Base64.hpp"
#include "Utils.hpp"
#include "strings.hpp"
#include "options.hpp"

namespace sqtop {

using std::string;
using std::map;
using std::vector;
using std::set;
using std::cout;
using std::endl;

/* static */ bool sqstat::CompareURLs(UriStats a, UriStats b) {
     return a.size > b.size;
}

/* static */ bool sqstat::CompareIP(SquidConnection a, SquidConnection b) {
   unsigned long ip1, ip2;
   struct sockaddr_in n;
   inet_aton(a.peer.c_str(), &n.sin_addr);
   ip1 = ntohl(n.sin_addr.s_addr);
   inet_aton(b.peer.c_str(), &n.sin_addr);
   ip2 = ntohl(n.sin_addr.s_addr);
   return ip1 < ip2;
}

/* static */ bool sqstat::CompareSIZE(SquidConnection a, SquidConnection b) {
     return a.sum_size > b.sum_size;
}

/* static */ bool sqstat::CompareTIME(SquidConnection a, SquidConnection b) {
   return a.max_etime > b.max_etime;
}

/* static */ bool sqstat::CompareAVSPEED(SquidConnection a, SquidConnection b) {
   return a.av_speed > b.av_speed;
}

/* static */ bool sqstat::CompareCURRSPEED(SquidConnection a, SquidConnection b) {
   return a.curr_speed > b.curr_speed;
}

/* static */ void sqstat::CompactSameUrls(vector<SquidConnection>& sqconns) {
   for (vector<SquidConnection>::iterator it = sqconns.begin(); it != sqconns.end(); ++it) {
      std::map<string, UriStats> urls;

      for (vector<UriStats>::iterator itu = it->stats.begin(); itu != it->stats.end(); ++itu) {
         string url = itu->uri;
         // TODO: check if username is the same ?
         if (urls.find(url) == urls.end()) {
            urls[url] = *itu;
         }
         else {
            urls[url].count += 1;
            urls[url].size += itu->size;
            urls[url].etime += itu->etime;
            // TODO: check this
            if ((urls[url].size !=0) && (urls[url].etime != 0))
               urls[url].av_speed = urls[url].size/urls[url].etime;
         }
      }

      it->stats.clear();
      for (std::map<string, UriStats>::iterator itm=urls.begin(); itm!=urls.end(); itm++) {
         it->stats.push_back(itm->second);
      }
      sort(it->stats.begin(), it->stats.end(), CompareURLs);
   }
}

/* static */ string sqstat::HeadFormat(Options* pOpts, int active_conn, int active_ips, long av_speed) {
   std::stringstream result;
   if ((pOpts->Hosts.size() == 0) && (pOpts->Users.size() == 0)) {
      result << endl << "Active connections: " << active_conn;
      result << ", active hosts: " << active_ips;
      if (pOpts->zero || (av_speed > 103))
         result << ", average speed: " << Utils::ConvertSpeed(av_speed);
      result << endl;
   }
   return result.str();
}

/* static */ string sqstat::ConnFormat(Options* pOpts, SquidConnection& scon) {
   std::stringstream result;

   result << "  Host: ";
#ifdef WITH_RESOLVER
   string resolved;
   if (pOpts->dns_resolution) {
      string tmp = scon.hostname;
      if (pOpts->strip_host_domain) {
         Resolver::StripDomain(tmp);
      }
      switch (pOpts->resolve_mode) {
         case Options::SHOW_NAME:
            resolved = tmp;
            break;
         case Options::SHOW_IP:
            resolved = scon.peer;
            break;
         case Options::SHOW_BOTH:
            if (!tmp.compare(scon.peer)) {
               resolved = scon.peer;
            } else {
               resolved = tmp + " [" + scon.peer + "]";
            }
            break;
      };
   } else {
      resolved = scon.peer;
   }
   result << resolved;
#else
   result << scon.peer;
#endif
   if (!scon.usernames.empty()) {
      set<string> users;
      if (pOpts->strip_user_domain) {
         for (set<string>::iterator it = scon.usernames.begin(); it != scon.usernames.end(); ++it) {
            users.insert(Utils::StripUserDomain(*it));
         }
      } else {
         users = scon.usernames;
      }
      result << "; " << (users.size() == 1 ? "User: " : "Users: ") << Utils::UsernamesToStr(users);
   }

   string condetail="";
   if (pOpts->full || pOpts->brief)
      condetail += "sessions: " + Utils::itos(scon.stats.size()) + ", ";
   if (pOpts->zero || (scon.sum_size > 1024))
      condetail += "size: " + Utils::ConvertSize(scon.sum_size) + ", ";
   if (pOpts->zero || (scon.curr_speed > 103) || (scon.av_speed > 103)) {
      condetail += SpeedsFormat(pOpts->speed_mode, scon.av_speed, scon.curr_speed) + ", ";
   }
   if (pOpts->full && (pOpts->zero || scon.max_etime > 0))
      condetail += "max time: " + Utils::ConvertTime(scon.max_etime) + ", ";
   if (condetail.size() > 2) {
      condetail.resize(condetail.size()-2);
      result << " (" << condetail << ")";
   }
   return result.str();
}

string sqstat::SpeedsFormat(Options::SPEED_MODE mode, long av_speed, long curr_speed) {
   std::stringstream result;
   std::pair <string, string> av_speed_pair;
   std::pair <string, string> curr_speed_pair;
   av_speed_pair = Utils::ConvertSpeedPair(av_speed);
   /*if ((curr_speed == 0) && (mode == Options::SPEED_MIXED)) {
      mode = Options::SPEED_AVERAGE;
   }*/
   switch (mode) {
      case Options::SPEED_CURRENT:
         curr_speed_pair = Utils::ConvertSpeedPair(curr_speed);
         result << "current speed: " << curr_speed_pair.first << curr_speed_pair.second;
         break;
      case Options::SPEED_MIXED:
         if ((curr_speed != av_speed) && (curr_speed > 103)) {
            std::pair <string, string> curr_speed_pair;
            curr_speed_pair = Utils::ConvertSpeedPair(curr_speed);
            result << "current/average speed: ";
            if (av_speed_pair.second == curr_speed_pair.second) {
               result << curr_speed_pair.first << "/" << av_speed_pair.first << " " << av_speed_pair.second;
            } else {
               result << curr_speed_pair.first << curr_speed_pair.second << " / " << av_speed_pair.first << av_speed_pair.second;
            }
            break;
         }
      case Options::SPEED_AVERAGE:
         result << "average speed: " + av_speed_pair.first + " " + av_speed_pair.second;
         break;
   };
   return result.str();
}

/* static */ string sqstat::StatFormat(Options* pOpts, SquidConnection& scon, UriStats& ustat) {
   std::stringstream result;
   result << "    " << ustat.uri;
   string udetail = "";
   if (ustat.count > 1) {
      udetail += "count: " + Utils::itos(ustat.count) + ", ";
   }
   if (pOpts->detail) {
      if (pOpts->zero || (ustat.size > 1024))
         udetail += "size: " + Utils::ConvertSize(ustat.size) + ", ";
      if (pOpts->full && ((pOpts->zero || (ustat.etime > 0))))
         udetail += "time: " + Utils::ConvertTime(ustat.etime) + ", ";
      if (scon.usernames.size() > 1)
         udetail += "user: " + (pOpts->strip_user_domain ? Utils::StripUserDomain(ustat.username) : ustat.username ) + ", ";
      if (pOpts->zero || (ustat.av_speed > 103) || (ustat.curr_speed > 103))
         udetail += SpeedsFormat(pOpts->speed_mode, ustat.av_speed, ustat.curr_speed) + ", ";
      if (pOpts->full && (pOpts->zero || (ustat.delay_pool != 0)))
         udetail += "delay_pool: " + Utils::itos(ustat.delay_pool) + ", ";
   }
   if (udetail.size() > 2) {
      udetail.resize(udetail.size()-2);
      result << " (" << udetail << ")";
   }
   return result.str();
}

void sqstat::FormatChanged(string line) {
   std::stringstream result;
   result << "Warning!!! Please send bug report.";
   result << " active_requests format changed - \'" << line << "\'.";
   result << " " << squid_version << ".";
   result << " " << PACKAGE_NAME << "-" << VERSION;
   throw sqstatException(result.str(), FORMAT_CHANGED);
}

#ifdef WITH_RESOLVER
string sqstat::DoResolve(string peer) {
   string resolved;
   if (pOpts->dns_resolution) {
      resolved = pResolver->Resolve(peer);
   } else {
      resolved = peer;
   }
   return resolved;
}
#endif

SquidStats sqstat::GetInfo() {
   sqconn con;

   string line;

   sqstats.total_connections = 0;

   map <string, SquidConnection>::iterator Conn_it; // pointer to current peer
   vector<UriStats>::iterator Stat_it; // pointer to current stat
   UriStats newStats;

   try {
      con.open(pOpts->host, pOpts->port);
   } catch(sqconnException &e) {
      std::stringstream error;
      error << e.what() << " while connecting to " << pOpts->host << ":" << pOpts->port;
      throw sqstatException(error.str(), FAILED_TO_CONNECT);
   }

   connections.clear();

   // TODO: use milliseconds from <chrono>
   time_t time_before_get = 0, time_before_process = 0;

   vector<string> headers;
   vector<string> active_requests;

   try {
      string request = "GET cache_object://localhost/active_requests HTTP/1.0\n";
      if (!pOpts->pass.empty()) {
         string encoded = Base64::Encode("admin:" + pOpts->pass);
         request += "Authorization: Basic " + encoded + "\n";
      }
      time_before_get = time(NULL);
      con << request;
      con.get(headers, active_requests);
      sqstats.get_time = time(NULL) - time_before_get;
   } catch(sqconnException &e) {
      throw sqstatException(e.what(), UNKNOWN_ERROR);
   }
   //return sqstats;

   time_before_process = time(NULL);
   if (headers.size() < 1) {
      throw sqstatException("Empty reply from squid", UNKNOWN_ERROR);
   } else if (headers[0] != "HTTP/1.0 200 OK" &&
              headers[0] != "HTTP/1.1 200 OK") {
      throw sqstatException("Access to squid statistic denied: '"+ headers[0] + "'", ACCESS_DENIED);
   }

   for (vector<string>::iterator it = active_requests.begin(); it != active_requests.end(); ++it) {
      line = *it;

      vector<string> result;
      if (line.substr(0,8) == "Server: ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 2) {
            squid_version = result[1];
         } else { FormatChanged(line); }
      } else if (line.substr(0,12) == "Connection: ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 2) {
            newStats = UriStats(result[1]);
         } else { FormatChanged(line); }
      } else if ((line.substr(0,6) == "peer: ") or (line.substr(0,8) == "remote: ")) {
         result = Utils::SplitString(line, " ");
         if (result.size() == 2) {
            std::pair <string, string> peer = Utils::SplitIPPort(result[1]);
            if (!peer.first.empty()) {
               Conn_it = connections.find(peer.first);
               // if it is new peer, create new SquidConnection
               if (Conn_it == connections.end()) {
                  SquidConnection connection;
                  connection.peer = peer.first;
#ifdef WITH_RESOLVER
                  connection.hostname = DoResolve(peer.first);
#endif
                  Conn_it = connections.insert( std::pair<string, SquidConnection>(peer.first, connection) ).first;
               }
               Conn_it->second.stats.push_back(newStats);
               Stat_it = Conn_it->second.stats.end() - 1;
            }
         } else { FormatChanged(line); }
      } else if (line.substr(0,4) == "uri ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 2) {
            Stat_it->uri = result[1];
            Stat_it->count = 1;
            Stat_it->curr_speed = 0;
            Stat_it->av_speed = 0;
         } else { FormatChanged(line); }
      } else if (line.substr(0,11) == "out.offset ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 4) {
            Stat_it->size = atoll(result[3].c_str());
            Conn_it->second.sum_size += Stat_it->size;
         } else { FormatChanged(line); }
      } else if (line.substr(0,6) == "start ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 5) {
            Stat_it->etime = atoi(result[2].erase(0,1).c_str());
            if (Stat_it->etime > Conn_it->second.max_etime)
               Conn_it->second.max_etime = Stat_it->etime;

            map<string, OldStat>::iterator size_it = oldstats.find(Stat_it->id);
            // store old progress in new connection stat
            Stat_it->oldsize = size_it != oldstats.end() ? size_it->second.size : 0;
            Stat_it->oldetime = size_it != oldstats.end() ? size_it->second.etime : 0;
            // replace old progress with new
            oldstats[Stat_it->id] = OldStat(Stat_it->size, Stat_it->etime);

         } else { FormatChanged(line); }
      } else if (line.substr(0,11) == "delay_pool ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 2) {
            Stat_it->delay_pool = atoi(result[1].c_str());
         } else { FormatChanged(line); }
      } else if (line.substr(0,9) == "username ") {
         result = Utils::SplitString(line, " ");
         if (result.size() == 1) {
            continue;
         } else if (result.size() == 2) {
            if ((result[1] != "-") && (!result[1].empty())) {
               Stat_it->username = result[1];
               Utils::ToLower(Stat_it->username);
               Conn_it->second.usernames.insert(Stat_it->username);
            }
         } else { FormatChanged(line); }
      }
   }

   sqstats.av_speed = 0;
   sqstats.curr_speed = 0;
   sqstats.connections.clear();
   for (map<string, SquidConnection>::iterator Conn = connections.begin(); Conn != connections.end(); ++Conn) {
      sqstats.total_connections += Conn->second.stats.size();

      for (vector<UriStats>::iterator Stats = Conn->second.stats.begin(); Stats != Conn->second.stats.end(); ++Stats) {
         if ((Stats->size != 0) && (Stats->etime != 0)) {
            Stats->av_speed = Stats->size/Stats->etime;
            Conn->second.av_speed += Stats->av_speed;
            sqstats.av_speed += Stats->av_speed;
         }
         if ((Stats->size != 0) && (Stats->oldsize != 0) &&
             (Stats->etime != 0) && (Stats->oldetime != 0) &&
             (Stats->size > Stats->oldsize) &&
             (Stats->etime > Stats->oldetime)) {
            long time_between_get = Stats->etime - Stats->oldetime;
            if (time_between_get < 1) time_between_get = 1;
            Stats->curr_speed = (Stats->size - Stats->oldsize) / time_between_get;
            Conn->second.curr_speed += Stats->curr_speed;
            sqstats.curr_speed += Stats->curr_speed;
         }
      }
      sqstats.connections.push_back(Conn->second);
   }
   sqstats.process_time = time(NULL) - time_before_process;

   return sqstats;
}

}
// vim: ai ts=3 sts=3 et sw=3 expandtab
