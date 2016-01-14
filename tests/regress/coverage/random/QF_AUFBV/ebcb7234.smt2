(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_AUFBV)
(declare-fun v0 () (_ BitVec 10))
(declare-fun v1 () (_ BitVec 15))
(declare-fun v2 () (_ BitVec 9))
(declare-fun v3 () (_ BitVec 12))
(declare-fun v4 () (_ BitVec 10))
(declare-fun a5 () (Array  (_ BitVec 6)  (_ BitVec 15)))
(assert (let ((e6(_ bv5 3)))
(let ((e7(_ bv11727 14)))
(let ((e8 (! (ite (distinct ((_ zero_extend 1) e7) v1) (_ bv1 1) (_ bv0 1)) :named term8)))
(let ((e9 (! (ite (bvuge ((_ sign_extend 2) v0) v3) (_ bv1 1) (_ bv0 1)) :named term9)))
(let ((e10 (! (bvurem ((_ zero_extend 5) v4) v1) :named term10)))
(let ((e11 (! (bvsrem ((_ zero_extend 2) e9) e6) :named term11)))
(let ((e12 (! (ite (bvsgt ((_ zero_extend 3) v2) v3) (_ bv1 1) (_ bv0 1)) :named term12)))
(let ((e13 (! (store a5 ((_ extract 5 0) v3) e10) :named term13)))
(let ((e14 (! (store e13 ((_ extract 8 3) v3) ((_ zero_extend 5) v0)) :named term14)))
(let ((e15 (! (select e14 ((_ zero_extend 5) e8)) :named term15)))
(let ((e16 (! (select e14 ((_ sign_extend 5) e9)) :named term16)))
(let ((e17 (! (store e13 ((_ sign_extend 5) e8) ((_ zero_extend 14) e8)) :named term17)))
(let ((e18 (! (select e14 ((_ extract 9 4) v4)) :named term18)))
(let ((e19 (! (ite (= (_ bv1 1) ((_ extract 0 0) e9)) e7 ((_ sign_extend 4) v0)) :named term19)))
(let ((e20 (! ((_ zero_extend 4) e9) :named term20)))
(let ((e21 (! (ite (bvsge e16 ((_ zero_extend 1) e19)) (_ bv1 1) (_ bv0 1)) :named term21)))
(let ((e22 (! (ite (bvsge ((_ zero_extend 3) v3) v1) (_ bv1 1) (_ bv0 1)) :named term22)))
(let ((e23 (! (bvnand e18 e10) :named term23)))
(let ((e24 (! (ite (bvule v3 ((_ sign_extend 11) e21)) (_ bv1 1) (_ bv0 1)) :named term24)))
(let ((e25 (! (ite (bvslt v4 ((_ zero_extend 7) e6)) (_ bv1 1) (_ bv0 1)) :named term25)))
(let ((e26 (! (bvor e18 ((_ zero_extend 6) v2)) :named term26)))
(let ((e27 (! (ite (bvuge ((_ sign_extend 2) e12) e6) (_ bv1 1) (_ bv0 1)) :named term27)))
(let ((e28 (! (bvnot e15) :named term28)))
(let ((e29 (! (ite (bvsge ((_ zero_extend 14) e8) e15) (_ bv1 1) (_ bv0 1)) :named term29)))
(let ((e30 (! (bvnand ((_ sign_extend 9) e11) v3) :named term30)))
(let ((e31 (! (bvsgt e15 ((_ sign_extend 10) e20)) :named term31)))
(let ((e32 (! (bvult v1 e16) :named term32)))
(let ((e33 (! (distinct ((_ sign_extend 6) v2) e28) :named term33)))
(let ((e34 (! (bvslt e18 ((_ zero_extend 12) e11)) :named term34)))
(let ((e35 (! (bvslt ((_ zero_extend 5) e20) v4) :named term35)))
(let ((e36 (! (bvult ((_ zero_extend 4) v4) e19) :named term36)))
(let ((e37 (! (bvslt e28 ((_ zero_extend 5) v0)) :named term37)))
(let ((e38 (! (= e16 e23) :named term38)))
(let ((e39 (! (bvult e16 e16) :named term39)))
(let ((e40 (! (= ((_ zero_extend 2) v0) e30) :named term40)))
(let ((e41 (! (bvult ((_ zero_extend 11) e8) e30) :named term41)))
(let ((e42 (! (bvsgt e16 ((_ zero_extend 12) e6)) :named term42)))
(let ((e43 (! (bvsge ((_ zero_extend 3) e30) e28) :named term43)))
(let ((e44 (! (distinct ((_ zero_extend 14) e21) e10) :named term44)))
(let ((e45 (! (bvsgt v4 ((_ zero_extend 9) e9)) :named term45)))
(let ((e46 (! (bvult ((_ zero_extend 10) e20) e23) :named term46)))
(let ((e47 (! (bvult e23 e15) :named term47)))
(let ((e48 (! (bvule v1 ((_ sign_extend 10) e20)) :named term48)))
(let ((e49 (! (bvsgt e15 e10) :named term49)))
(let ((e50 (! (bvult e15 ((_ zero_extend 1) e7)) :named term50)))
(let ((e51 (! (bvsge ((_ sign_extend 2) e9) e6) :named term51)))
(let ((e52 (! (bvsgt v3 ((_ sign_extend 11) e21)) :named term52)))
(let ((e53 (! (bvule v1 ((_ zero_extend 3) v3)) :named term53)))
(let ((e54 (! (bvsge e9 e24) :named term54)))
(let ((e55 (! (bvuge ((_ sign_extend 14) e27) e28) :named term55)))
(let ((e56 (! (bvsge e28 ((_ zero_extend 14) e8)) :named term56)))
(let ((e57 (! (bvugt ((_ zero_extend 14) e8) e18) :named term57)))
(let ((e58 (! (distinct e29 e8) :named term58)))
(let ((e59 (! (bvule e7 ((_ sign_extend 13) e9)) :named term59)))
(let ((e60 (! (distinct e16 ((_ zero_extend 6) v2)) :named term60)))
(let ((e61 (! (= e9 e9) :named term61)))
(let ((e62 (! (bvsle ((_ zero_extend 9) e11) v3) :named term62)))
(let ((e63 (! (bvult ((_ zero_extend 14) e8) e23) :named term63)))
(let ((e64 (! (bvuge e20 ((_ zero_extend 4) e27)) :named term64)))
(let ((e65 (! (bvslt e11 ((_ sign_extend 2) e8)) :named term65)))
(let ((e66 (! (bvule e10 ((_ sign_extend 6) v2)) :named term66)))
(let ((e67 (! (bvugt ((_ sign_extend 9) e21) v0) :named term67)))
(let ((e68 (! (bvsle ((_ zero_extend 9) e12) v4) :named term68)))
(let ((e69 (! (bvsgt v4 ((_ zero_extend 9) e22)) :named term69)))
(let ((e70 (! (distinct e12 e22) :named term70)))
(let ((e71 (! (bvsle v4 ((_ sign_extend 1) v2)) :named term71)))
(let ((e72 (! (bvult ((_ zero_extend 5) v4) e10) :named term72)))
(let ((e73 (! (bvsgt ((_ sign_extend 14) e24) e23) :named term73)))
(let ((e74 (! (bvsle e18 ((_ sign_extend 14) e27)) :named term74)))
(let ((e75 (! (bvult ((_ zero_extend 3) v3) v1) :named term75)))
(let ((e76 (! (bvuge ((_ zero_extend 4) v0) e19) :named term76)))
(let ((e77 (! (bvsle ((_ sign_extend 3) e30) v1) :named term77)))
(let ((e78 (! (bvsgt e6 ((_ zero_extend 2) e24)) :named term78)))
(let ((e79 (! (bvugt ((_ zero_extend 5) v0) e18) :named term79)))
(let ((e80 (! (= e15 e16) :named term80)))
(let ((e81 (! (bvule ((_ zero_extend 5) v4) e16) :named term81)))
(let ((e82 (! (bvsle e27 e12) :named term82)))
(let ((e83 (! (bvslt v1 ((_ sign_extend 10) e20)) :named term83)))
(let ((e84 (! (bvsge ((_ sign_extend 8) e9) v2) :named term84)))
(let ((e85 (! (bvslt v1 ((_ sign_extend 12) e6)) :named term85)))
(let ((e86 (! (bvsgt e19 ((_ zero_extend 2) e30)) :named term86)))
(let ((e87 (! (bvslt e23 ((_ zero_extend 6) v2)) :named term87)))
(let ((e88 (! (bvult v0 ((_ sign_extend 9) e25)) :named term88)))
(let ((e89 (! (bvslt v0 v0) :named term89)))
(let ((e90 (! (= e15 e23) :named term90)))
(let ((e91 (! (bvuge ((_ zero_extend 12) e6) e23) :named term91)))
(let ((e92 (! (bvule v0 ((_ zero_extend 1) v2)) :named term92)))
(let ((e93 (! (bvsgt ((_ zero_extend 7) e6) v4) :named term93)))
(let ((e94 (! (bvugt ((_ zero_extend 13) e21) e19) :named term94)))
(let ((e95 (! (bvuge ((_ zero_extend 3) e30) e15) :named term95)))
(let ((e96 (! (= ((_ zero_extend 8) e24) v2) :named term96)))
(let ((e97 (! (bvule ((_ zero_extend 14) e12) e28) :named term97)))
(let ((e98 (! (bvsge e23 e10) :named term98)))
(let ((e99 (! (bvule e22 e27) :named term99)))
(let ((e100 (! (distinct e28 v1) :named term100)))
(let ((e101 (! (bvsgt e26 e26) :named term101)))
(let ((e102 (! (and e91 e37) :named term102)))
(let ((e103 (! (ite e62 e36 e67) :named term103)))
(let ((e104 (! (or e69 e97) :named term104)))
(let ((e105 (! (or e56 e63) :named term105)))
(let ((e106 (! (xor e44 e92) :named term106)))
(let ((e107 (! (= e99 e45) :named term107)))
(let ((e108 (! (= e77 e59) :named term108)))
(let ((e109 (! (xor e46 e84) :named term109)))
(let ((e110 (! (ite e86 e96 e104) :named term110)))
(let ((e111 (! (or e93 e66) :named term111)))
(let ((e112 (! (or e81 e90) :named term112)))
(let ((e113 (! (not e83) :named term113)))
(let ((e114 (! (ite e42 e55 e105) :named term114)))
(let ((e115 (! (= e79 e100) :named term115)))
(let ((e116 (! (xor e50 e31) :named term116)))
(let ((e117 (! (= e58 e71) :named term117)))
(let ((e118 (! (=> e38 e65) :named term118)))
(let ((e119 (! (or e54 e47) :named term119)))
(let ((e120 (! (and e60 e57) :named term120)))
(let ((e121 (! (xor e51 e49) :named term121)))
(let ((e122 (! (and e108 e76) :named term122)))
(let ((e123 (! (xor e68 e109) :named term123)))
(let ((e124 (! (not e106) :named term124)))
(let ((e125 (! (xor e33 e32) :named term125)))
(let ((e126 (! (= e102 e114) :named term126)))
(let ((e127 (! (ite e78 e89 e48) :named term127)))
(let ((e128 (! (= e123 e122) :named term128)))
(let ((e129 (! (or e121 e118) :named term129)))
(let ((e130 (! (and e52 e72) :named term130)))
(let ((e131 (! (or e41 e125) :named term131)))
(let ((e132 (! (or e103 e95) :named term132)))
(let ((e133 (! (ite e119 e130 e116) :named term133)))
(let ((e134 (! (or e53 e129) :named term134)))
(let ((e135 (! (=> e112 e133) :named term135)))
(let ((e136 (! (xor e124 e73) :named term136)))
(let ((e137 (! (and e74 e135) :named term137)))
(let ((e138 (! (and e107 e35) :named term138)))
(let ((e139 (! (xor e98 e115) :named term139)))
(let ((e140 (! (= e70 e39) :named term140)))
(let ((e141 (! (not e75) :named term141)))
(let ((e142 (! (or e117 e117) :named term142)))
(let ((e143 (! (xor e87 e134) :named term143)))
(let ((e144 (! (ite e142 e143 e111) :named term144)))
(let ((e145 (! (ite e80 e40 e139) :named term145)))
(let ((e146 (! (not e64) :named term146)))
(let ((e147 (! (ite e61 e88 e126) :named term147)))
(let ((e148 (! (and e147 e132) :named term148)))
(let ((e149 (! (xor e34 e34) :named term149)))
(let ((e150 (! (ite e146 e43 e140) :named term150)))
(let ((e151 (! (xor e149 e127) :named term151)))
(let ((e152 (! (not e150) :named term152)))
(let ((e153 (! (or e148 e94) :named term153)))
(let ((e154 (! (ite e110 e145 e144) :named term154)))
(let ((e155 (! (or e151 e101) :named term155)))
(let ((e156 (! (or e141 e141) :named term156)))
(let ((e157 (! (or e137 e128) :named term157)))
(let ((e158 (! (=> e154 e120) :named term158)))
(let ((e159 (! (not e85) :named term159)))
(let ((e160 (! (ite e157 e131 e156) :named term160)))
(let ((e161 (! (or e152 e159) :named term161)))
(let ((e162 (! (xor e138 e113) :named term162)))
(let ((e163 (! (not e161) :named term163)))
(let ((e164 (! (=> e153 e158) :named term164)))
(let ((e165 (! (=> e162 e164) :named term165)))
(let ((e166 (! (ite e163 e136 e165) :named term166)))
(let ((e167 (! (=> e160 e155) :named term167)))
(let ((e168 (! (= e166 e167) :named term168)))
(let ((e169 (! (xor e168 e82) :named term169)))
(let ((e170 (! (and e169 (not (= v1 (_ bv0 15)))) :named term170)))
(let ((e171 (! (and e170 (not (= e6 (_ bv0 3)))) :named term171)))
(let ((e172 (! (and e171 (not (= e6 (bvnot (_ bv0 3))))) :named term172)))
e172
))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
(get-value (term8))
(get-value (term9))
(get-value (term10))
(get-value (term11))
(get-value (term12))
(get-value (term13))
(get-value (term14))
(get-value (term15))
(get-value (term16))
(get-value (term17))
(get-value (term18))
(get-value (term19))
(get-value (term20))
(get-value (term21))
(get-value (term22))
(get-value (term23))
(get-value (term24))
(get-value (term25))
(get-value (term26))
(get-value (term27))
(get-value (term28))
(get-value (term29))
(get-value (term30))
(get-value (term31))
(get-value (term32))
(get-value (term33))
(get-value (term34))
(get-value (term35))
(get-value (term36))
(get-value (term37))
(get-value (term38))
(get-value (term39))
(get-value (term40))
(get-value (term41))
(get-value (term42))
(get-value (term43))
(get-value (term44))
(get-value (term45))
(get-value (term46))
(get-value (term47))
(get-value (term48))
(get-value (term49))
(get-value (term50))
(get-value (term51))
(get-value (term52))
(get-value (term53))
(get-value (term54))
(get-value (term55))
(get-value (term56))
(get-value (term57))
(get-value (term58))
(get-value (term59))
(get-value (term60))
(get-value (term61))
(get-value (term62))
(get-value (term63))
(get-value (term64))
(get-value (term65))
(get-value (term66))
(get-value (term67))
(get-value (term68))
(get-value (term69))
(get-value (term70))
(get-value (term71))
(get-value (term72))
(get-value (term73))
(get-value (term74))
(get-value (term75))
(get-value (term76))
(get-value (term77))
(get-value (term78))
(get-value (term79))
(get-value (term80))
(get-value (term81))
(get-value (term82))
(get-value (term83))
(get-value (term84))
(get-value (term85))
(get-value (term86))
(get-value (term87))
(get-value (term88))
(get-value (term89))
(get-value (term90))
(get-value (term91))
(get-value (term92))
(get-value (term93))
(get-value (term94))
(get-value (term95))
(get-value (term96))
(get-value (term97))
(get-value (term98))
(get-value (term99))
(get-value (term100))
(get-value (term101))
(get-value (term102))
(get-value (term103))
(get-value (term104))
(get-value (term105))
(get-value (term106))
(get-value (term107))
(get-value (term108))
(get-value (term109))
(get-value (term110))
(get-value (term111))
(get-value (term112))
(get-value (term113))
(get-value (term114))
(get-value (term115))
(get-value (term116))
(get-value (term117))
(get-value (term118))
(get-value (term119))
(get-value (term120))
(get-value (term121))
(get-value (term122))
(get-value (term123))
(get-value (term124))
(get-value (term125))
(get-value (term126))
(get-value (term127))
(get-value (term128))
(get-value (term129))
(get-value (term130))
(get-value (term131))
(get-value (term132))
(get-value (term133))
(get-value (term134))
(get-value (term135))
(get-value (term136))
(get-value (term137))
(get-value (term138))
(get-value (term139))
(get-value (term140))
(get-value (term141))
(get-value (term142))
(get-value (term143))
(get-value (term144))
(get-value (term145))
(get-value (term146))
(get-value (term147))
(get-value (term148))
(get-value (term149))
(get-value (term150))
(get-value (term151))
(get-value (term152))
(get-value (term153))
(get-value (term154))
(get-value (term155))
(get-value (term156))
(get-value (term157))
(get-value (term158))
(get-value (term159))
(get-value (term160))
(get-value (term161))
(get-value (term162))
(get-value (term163))
(get-value (term164))
(get-value (term165))
(get-value (term166))
(get-value (term167))
(get-value (term168))
(get-value (term169))
(get-value (term170))
(get-value (term171))
(get-value (term172))
(get-info :all-statistics)