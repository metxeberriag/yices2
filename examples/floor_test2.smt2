(set-logic QF_LIRA)
(declare-fun x () Real)
(assert (> x (to_int x)))
(check-sat)
(get-model)
(get-value (x (to_int x) (abs x)))